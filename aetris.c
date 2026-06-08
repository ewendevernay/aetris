#include "basic.c"
#include "serialize.c"

#include "aetris.h"
#include "raylib.h"

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#define CRASH(error) printf("%s:%d: %s\n", __FILE__, __LINE__, error); abort();

#define DEFAULT_SERVER_PORT 6966

#define ITEMS_TEXT_HEIGHT 30
#define ITEMS_PADDING 100

static int mouse_x;
static int mouse_y;

static int screen_width;
static int screen_height;

typedef uint32_t PlayerID;

#define MAX_PLAYERS 4

#define BOARD_WIDTH 30
#define BOARD_HEIGHT 30
#define BOARD_AT(x, y) ((y) * BOARD_WIDTH + (x))

#define PIECE_WIDTH 4
#define PIECE_HEIGHT 3
#define PIECE_SIZE PIECE_WIDTH * PIECE_HEIGHT
#define PIECE_ORIGIN_X 2
#define PIECE_ORIGIN_Y 1
#define PIECE_AT(x, y) ((y) * PIECE_WIDTH + (x))

static Color tetris_pieces_colors[] = {
	{ 0, 0, 134, 255 },
	{ 192, 100, 100, 255 },
	{ 50, 120, 50, 255 },
	{ 50, 10, 100, 255 },
	{ 123, 0, 0, 255 },
	{ 0, 133, 0, 255 },
	{ 100, 87, 98, 255 }
};

static uint8_t tetris_pieces[] = {
	0, 0, 0, 0,
	1, 1, 1, 1,
	0, 0, 0, 0,

	1, 0, 0, 0,
	1, 1, 1, 0,
	0, 0, 0, 0,

	0, 0, 1, 0,
	1, 1, 1, 0,
	0, 0, 0, 0,

	0, 1, 0, 0,
	1, 1, 1, 0,
	0, 0, 0, 0,

	0, 0, 0, 0,
	0, 1, 1, 0,
	1, 1, 0, 0,

	0, 0, 0, 0,
	1, 1, 0, 0,
	0, 1, 1, 0,

	0, 0, 0, 0,
	0, 1, 1, 0,
	0, 1, 1, 0,
};

#define PIECES_COUNT ARRAY_COUNT(tetris_pieces) / (PIECE_SIZE)

/*
		# Client Packet format

		uint32_t Packet ID
		uint8_t Packet type: ACK, HELLO, BYE, MOVE LEFT, MOVE RIGHT, MOVE DOWN, MOVE ROTATE, MOVE FALL

		# Server packet format

		uint32_t Packet ID
		uint8_t Packet type: ACK, BOARD, PLAYERS
*/

#define PACKET_MAX_SIZE 2048

#define UNTIL_HELD_DOWN 8
#define KEY_PRESS_FREQ 2

typedef enum {
	P_ACK = 0,
	P_HELLO,
	P_BYE,
	P_MOVE_LEFT,
	P_MOVE_RIGHT,
	P_MOVE_ROTATE,
	P_MOVE_FALL,
	P_BOARD,
	P_PLAYERS
} PacketType;

typedef struct {
	int pos_x;
	int pos_y;

	uint8_t piece_id;
	uint8_t piece_rot;

	uint32_t packets_sent_to;
	AeAddr address;

	int starting_pos;
} Player;

#define DEFAULT_SPEED 25
#define MAX_SPEED 2

typedef struct {
	AeSock *socket;

	uint32_t players_count;
	Player players[MAX_PLAYERS];
	uint8_t players_active[MAX_PLAYERS]; // TODO: Better player ID handling

	uint8_t board[BOARD_WIDTH * BOARD_HEIGHT];

	int32_t until_next_tick;
	int32_t speed;
} ServerState;

enum {
	K_LEFT,
	K_RIGHT,
	K_UP,
	K_SPACE
} ClientKeys;

static uint32_t keys_of_interest[] = {
	[K_LEFT] = KEY_LEFT,
	[K_RIGHT] = KEY_RIGHT,
	[K_UP] = KEY_UP,
	[K_SPACE] = KEY_SPACE
};

typedef struct {
	AeSock *socket;
	AeAddr distant_server_addr;

	bool8_t has_board;
	bool8_t sent_hello;

	uint32_t packets_sent;

	uint8_t board[BOARD_WIDTH * BOARD_HEIGHT];

	uint32_t players_count;
	Player players[MAX_PLAYERS];
	uint32_t current_player_index;

	int32_t until_next_press[ARRAY_COUNT(keys_of_interest)];
	bool8_t being_pressed[ARRAY_COUNT(keys_of_interest)];
} ClientState;

static ServerState server_state;
static ClientState client_state;

static enum {
	S_TITLE_SCREEN,
	S_CONNECT,
	S_PLAYING
} aetris_state;

static bool8_t parse_ipv4(String str, uint8_t *ip) {
	uint8_t buffer[1024];
	Pool pool;
	pool_init(&pool, buffer, sizeof buffer);

	uint32_t numbers_count;
	String *numbers = string_split(&pool, str, '.', &numbers_count);

	if (numbers == 0 || numbers_count != 4) return false;

	for (uint32_t i = 0; i < 4; i++) {
		int64_t x;
		if (!string_to_int(numbers[i], &x)) return false;

		if (x < 0 || x > 255) return false;

		ip[i] = (uint8_t)x;
	}

	return true;
}

static bool8_t address_equal(AeAddr *a, AeAddr *b) {
	return memcmp(a, b, sizeof (AeAddr)) == 0;
}

static uint16_t le_to_be_u16(uint64_t x) {
	return ((uint16_t)(((uint8_t*)&x)[0]) << 8) | (uint16_t)(((uint8_t*)&x)[1]);
}

static void rotate_piece(int *x, int *y, uint8_t piece_id, uint8_t piece_rot) {
	for (uint32_t j = 0; j < piece_rot; j++) {
		int size = 3;
		if (piece_id >= 1 && piece_id <= 3) size = 2;

		int t_x = *x;
		int t_y = *y;

		*y = t_x;
		*x = size - t_y;
	}
}

static void server_send_packet(PacketType type, uint8_t *contents, uint64_t contents_size, Player *player) {
	uint8_t packet[PACKET_MAX_SIZE];

	se_begin(packet, sizeof packet);

	se_write_u32(player->packets_sent_to);
	se_write_u8(type);
	se_write_bytes(contents, contents_size);

	uint64_t packet_size;
	bool8_t success = se_end(&packet_size);

	if (!success) { CRASH("Packet too big!"); }

	uint64_t bytes_sent;
	if (send_packet(server_state.socket, &player->address, packet, packet_size, &bytes_sent) == R_READY &&
					bytes_sent == packet_size)
	{
		player->packets_sent_to++;
	} else {
		CRASH("Server couldn't send packet!\n");
	}
}

#define PACKED_PLAYER_SIZE 10

static void server_send_players(Player *player) {
	uint8_t contents[4 + PACKED_PLAYER_SIZE * MAX_PLAYERS];

	se_begin(contents, sizeof contents);
	se_write_u32(server_state.players_count);

	for (uint32_t i = 0; i < server_state.players_count; i++) {
		if (player == &server_state.players[i]) {
			se_write_u8(1);
		} else {
			se_write_u8(0);
		}

		se_write_s32(server_state.players[i].pos_x);
		se_write_s32(server_state.players[i].pos_y);

		se_write_u8(server_state.players[i].piece_id);
		se_write_u8(server_state.players[i].piece_rot);
	}

	uint64_t content_size = 0;
	se_end(&content_size);

	server_send_packet(P_PLAYERS, contents, content_size, player);
}

static void server_send_board(Player *player) {
	server_send_packet(P_BOARD, server_state.board, sizeof server_state.board, player);
}

static bool8_t engrave_player(uint8_t *board, Player *player) {
	bool8_t can_move = true;

	uint8_t piece_id = player->piece_id;
	uint8_t *piece = tetris_pieces + piece_id * PIECE_SIZE;

	for (int32_t x = 0; x < PIECE_WIDTH; x++) {
		for (int32_t y = 0; y < PIECE_HEIGHT; y++) {
			if (piece[PIECE_AT(x, y)]) {
				int rot_x = x;
				int rot_y = y;

				rotate_piece(&rot_x, &rot_y, piece_id, player->piece_rot);

				int real_x = rot_x - PIECE_ORIGIN_X + player->pos_x;
				int real_y = rot_y - PIECE_ORIGIN_Y + player->pos_y;

				if (real_x < 0 || real_x >= BOARD_WIDTH ||
								real_y >= BOARD_HEIGHT)
				{
					can_move = false;
					break; // TODO
				} else if (real_y < 0) {
					can_move = true;
					break; // TODO
				}

				if (board[BOARD_AT(real_x, real_y)] != 0) {
					can_move = false;
					break;
				}

				board[BOARD_AT(real_x, real_y)] = piece_id + 1;
			}
		}
	}

	return can_move;
}

#define BOARD_COLLISION 0
#define PLAYER_COLLISION 1
#define NO_COLLISION 2

// The bug is in this function: the other player's erroneous position blocks this player
// from moving correctly.
static int player_can_move(Player *players, uint32_t players_count, uint8_t *board, Player *player, int dx, int dy, bool8_t rotate) {
	int old_pos_x = player->pos_x;
	int old_pos_y = player->pos_y;

	player->pos_x += dx;
	player->pos_y += dy;

	uint8_t old_rot = player->piece_rot;
	if (rotate) player->piece_rot = (player->piece_rot + 1) % 4;

	uint8_t temp_board[BOARD_WIDTH * BOARD_HEIGHT];
	memcpy(temp_board, board, BOARD_WIDTH * BOARD_HEIGHT);

	bool8_t can_move = true;

	for (uint32_t i = 0; i < players_count; i++) {
		if (!engrave_player(temp_board, &players[i])) {
			can_move = false;
			break;
		}
	}

	if (!can_move) {
		memcpy(temp_board, board, BOARD_WIDTH * BOARD_HEIGHT);
		if (engrave_player(temp_board, player)) {
			player->pos_x = old_pos_x;
			player->pos_y = old_pos_y;

			player->piece_rot = old_rot;

			return PLAYER_COLLISION;
		} else {
			player->pos_x = old_pos_x;
			player->pos_y = old_pos_y;

			player->piece_rot = old_rot;

			return BOARD_COLLISION;
		}
	} else {
		player->pos_x = old_pos_x;
		player->pos_y = old_pos_y;

		player->piece_rot = old_rot;

		return NO_COLLISION;
	}
}

static int players_compare_y(const void *a, const void *b) {
	const Player *p1 = a;
	const Player *p2 = b;

	return p2->pos_y - p1->pos_y;
}

static void update_server() {
	uint8_t packet[PACKET_MAX_SIZE];
	AeAddr src_addr;
	uint64_t bytes_count;

	bool8_t should_send_players = false;
	bool8_t should_send_board = false;

	if (receive_packet(server_state.socket, packet, sizeof packet, &src_addr, &bytes_count) == R_READY &&
					bytes_count >= 5)
	{
		int player_index = -1;

		for (uint32_t i = 0; i < server_state.players_count; i++) {
			if (address_equal(&src_addr, &server_state.players[i].address)) {
				player_index = i;
				break;
			}
		}

		if (player_index < 0 && server_state.players_count < MAX_PLAYERS) {  // Player creation
			Player *player = &server_state.players[server_state.players_count];
			memzero(player);

			player_index = server_state.players_count;

			player->address = src_addr;
			player->packets_sent_to = 0;

			player->piece_id = rand() % PIECES_COUNT;

			for (player->starting_pos = 0; player->starting_pos < MAX_PLAYERS; player->starting_pos++) {
				bool8_t is_available = true;

				for (uint32_t i = 0; i < server_state.players_count; i++) {
					if (server_state.players[i].starting_pos == player->starting_pos) {
						is_available = false;
						break;
					}
				}

				if (is_available) break;
			}

			uint32_t chunk_size = BOARD_WIDTH / MAX_PLAYERS;

#define DEFAULT_POS_Y 0

			player->pos_x = player->starting_pos * chunk_size + chunk_size / 2;
			player->pos_y = DEFAULT_POS_Y;

			server_state.players_count++;
		}

		if (player_index >= 0) {
			Player *player = &server_state.players[player_index];

			switch (packet[4]) {
			case P_HELLO: {
				should_send_board = true;
				should_send_players = true;
			} break;
			case P_BYE: {
				Player temp = server_state.players[server_state.players_count - 1];
				server_state.players[server_state.players_count - 1] = server_state.players[player_index];
				server_state.players[player_index] = temp;

				server_state.players_count--;

				should_send_players = true;
			} break;
			case P_MOVE_RIGHT: {
				if (player_can_move(server_state.players, server_state.players_count, server_state.board,
																								player, 1, 0, false) == NO_COLLISION)
				{
					player->pos_x += 1;

					should_send_players = true;
				}
			} break;
			case P_MOVE_LEFT: {
				if (player_can_move(server_state.players, server_state.players_count, server_state.board,
																								player, -1, 0, false) == NO_COLLISION)
				{
					player->pos_x -= 1;

					should_send_players = true;
				}
			} break;
			case P_MOVE_FALL: {
				while (player_can_move(server_state.players, server_state.players_count, server_state.board,
																											player, 0, 1, false) == NO_COLLISION)
				{
					player->pos_y += 1;
					should_send_players = true;
				}
			} break;
			case P_MOVE_ROTATE: {
				if (player_can_move(server_state.players, server_state.players_count, server_state.board,
																								player, 0, 0, true) == NO_COLLISION)
				{
					player->piece_rot = (player->piece_rot + 1) % 4;

					should_send_players = true;
				}
			} break;
			default:
				printf("Invalid message!");
			}
		}
	}

	if (server_state.until_next_tick > 0) {
		server_state.until_next_tick -= 1;
	} else {
		server_state.until_next_tick = server_state.speed;

		uint32_t chunk_size = BOARD_WIDTH / MAX_PLAYERS;

		qsort(server_state.players, server_state.players_count, sizeof (Player), players_compare_y);

		// Check things
		for (uint32_t i = 0; i < server_state.players_count; i++) {
			Player *player = &server_state.players[i];
			int collision;

			if ((collision = player_can_move(server_state.players, server_state.players_count, server_state.board,
																																				player, 0, 1, false)) == BOARD_COLLISION)
			{ // We're at the bottom
				if (player->pos_y <= DEFAULT_POS_Y) { // Game over
					memset(server_state.board, 0, sizeof server_state.board);

					for (uint32_t j = 0; j < server_state.players_count; j++) {
						server_state.players[i].pos_x = server_state.players[i].starting_pos * chunk_size + chunk_size / 2;
						server_state.players[i].pos_y = DEFAULT_POS_Y;

						server_state.players[i].piece_id = rand() % PIECES_COUNT;
						server_state.speed = DEFAULT_SPEED;
					}

					should_send_board = true;
				} else {
					engrave_player(server_state.board, player);

					player->pos_x = player->starting_pos * chunk_size + chunk_size / 2;
					player->pos_y = DEFAULT_POS_Y;

					player->piece_id = rand() % PIECES_COUNT;

					should_send_board = true;

					int x = 0;
					int y = BOARD_HEIGHT - 1;
					while (y >= 0) {
						x = 0;
						bool8_t is_line = true;
						while (x < BOARD_WIDTH) {
							if (!server_state.board[BOARD_AT(x, y)]) {
								is_line = false;
								break;
							}
							x++;
						}

						if (is_line) {
							for (int ny = y - 1; ny >= 0; ny--) {
								for (int nx = 0; nx < BOARD_WIDTH; nx++) {
									server_state.board[BOARD_AT(nx, ny + 1)] = server_state.board[BOARD_AT(nx, ny)];
								}
							}

							server_state.speed = MAX(server_state.speed * 9 / 10, MAX_SPEED);
						} else {
							y--;
						}
					}
				}
			} else {
				server_state.players[i].pos_y++;
			}
		}

		// Then send them.
		should_send_players = true;
	}

	for (uint32_t i = 0; i < server_state.players_count; i++) {
		if (should_send_board)   server_send_board(&server_state.players[i]);
		if (should_send_players) server_send_players(&server_state.players[i]);
	}
}

static void client_send_packet(PacketType type, uint8_t *contents, uint64_t contents_size) {
	uint8_t packet[PACKET_MAX_SIZE];

	se_begin(packet, sizeof packet);

	se_write_u32(client_state.packets_sent);
	se_write_u8(type);
	se_write_bytes(contents, contents_size);

	uint64_t packet_size;
	bool8_t success = se_end(&packet_size);

	if (!success) { CRASH("Packet too big!"); }

	uint64_t bytes_sent;
	if (send_packet(client_state.socket, &client_state.distant_server_addr,
																	packet, packet_size, &bytes_sent) == R_READY &&
					bytes_sent == packet_size)
	{
		client_state.packets_sent++;
	} else {
		CRASH("Client couldn't send packet\n");
	}
}

static bool8_t menu_button(char *text, uint32_t position) {
	int text_width = MeasureText(text, ITEMS_TEXT_HEIGHT);

	int text_x = screen_width / 2 - text_width / 2;
	int text_y = screen_height / 12 + 150 + ITEMS_PADDING * position;

	bool8_t is_hovered = (mouse_x >= text_x && mouse_x <= text_x + text_width &&
																							mouse_y >= text_y && mouse_y <= text_y + ITEMS_TEXT_HEIGHT);

	DrawText(text, text_x, text_y, ITEMS_TEXT_HEIGHT, is_hovered ? RED : LIGHTGRAY);

	return is_hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static Rectangle piece_part_rectangle(Player *player, int x, int y, uint32_t x_offset, uint32_t square_size) {
	int rot_x = x;
	int rot_y = y;

	rotate_piece(&rot_x, &rot_y, player->piece_id, player->piece_rot);

	Rectangle rec = {
		x_offset + (player->pos_x + rot_x - PIECE_ORIGIN_X) * square_size,
		(player->pos_y + rot_y - PIECE_ORIGIN_Y) * square_size,
		square_size,
		square_size,
	};

	return rec;
}

// Code for the conditioner

#define COND_MAX_VALUES 16
#define MAX_EPOLL_EVENTS 32
#define HOVER_DIFF 6500 // About 7000 works

static int cond_serial_ports[2];
static uint64_t cond_values[ARRAY_COUNT(cond_serial_ports)][COND_MAX_VALUES] = {};
static uint64_t cond_values_start[ARRAY_COUNT(cond_serial_ports)] = {};
static uint64_t cond_means[ARRAY_COUNT(cond_serial_ports)];
static uint64_t cond_zeros[ARRAY_COUNT(cond_serial_ports)] = {};
static uint64_t cond_thresholds[ARRAY_COUNT(cond_serial_ports)] = {
	0, 0
};
static bool8_t cond_enabled = true;
static bool8_t cond_debug_graph = false;
static	int epoll_fd;

static bool8_t cond_left;
static bool8_t cond_right;
static bool8_t cond_turn_left;
static bool8_t cond_turn_right;

void cond_init() {
	epoll_fd = epoll_create1(0);

	cond_serial_ports[0] = open("/dev/ttyACM0", O_RDONLY);
	cond_serial_ports[1] = open("/dev/ttyACM1", O_RDONLY);

	if (cond_serial_ports[0] < 0 || cond_serial_ports[1] < 0) {
		cond_enabled = false;
	} else {		
		for (uint32_t i = 0; i < ARRAY_COUNT(cond_serial_ports); i++) {
			struct epoll_event com_event;
			com_event.events = EPOLLIN;
			com_event.data.u32 = i;
	
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cond_serial_ports[i], &com_event);
		}
	}
}

char cond_buffers[ARRAY_COUNT(cond_serial_ports)][1024] = {};
uint32_t cond_buffers_start[ARRAY_COUNT(cond_serial_ports)] = {};

bool8_t cond_switched_controls = false;

void cond_switch_controls() {
	cond_switched_controls = !cond_switched_controls;
}

void cond_update() {
	cond_left = false;
	cond_right = false;
	cond_turn_left = false;
	cond_turn_right = false;
	
	if (cond_enabled) {
		// Handle new values
		
		struct epoll_event epoll_events[MAX_EPOLL_EVENTS];
		int nfds = epoll_wait(epoll_fd, epoll_events, MAX_EPOLL_EVENTS, 0);

		for (int i = 0; i < nfds; i++) {
			uint32_t port_id = epoll_events[i].data.u32;

			ssize_t bytes_recieved = read(cond_serial_ports[port_id], cond_buffers[port_id] + cond_buffers_start[port_id], (sizeof cond_buffers[0]) - cond_buffers_start[port_id]);
			
			uint32_t i = 0;
			uint32_t last_semicolon = 0;
			for (i = 0; i < bytes_recieved + cond_buffers_start[port_id]; i++) {
				if (cond_buffers[port_id][i] == ';' && i != last_semicolon) {
					String number = {
						.data = cond_buffers[port_id] + last_semicolon,
						.count = i - last_semicolon
					};

					last_semicolon = i + 1;
					
					int64_t frequency = 0;
					string_to_int(number, &frequency);

					cond_values[port_id][cond_values_start[port_id]] = frequency;
					cond_values_start[port_id] = (cond_values_start[port_id] + 1) % COND_MAX_VALUES;

					// Do mean
					uint64_t sum = 0;
					for (uint32_t j = 0; j < COND_MAX_VALUES; j++) {
						sum += cond_values[port_id][j];
					}

					cond_means[port_id] = sum / COND_MAX_VALUES;

					//if (frequency < cond_zeros[port_id] - HOVER_DIFF) {

					uint64_t last_start_index = modi((int64_t)cond_values_start[port_id] - 2, COND_MAX_VALUES);

					int64_t x = cond_zeros[port_id] - frequency;
					int64_t last_x = cond_zeros[port_id] - cond_values[port_id][last_start_index];
					
					int64_t diff = (int64_t)cond_zeros[port_id] - (int64_t)cond_thresholds[port_id];
					int64_t error_margin = diff * 2 / 10;

					if (x > diff * 2) {
						if (port_id == cond_switched_controls) cond_right = true;
						if (port_id == !cond_switched_controls) cond_left = true;
					} else if (x > diff - error_margin && last_x < diff - error_margin) {
						if (port_id == cond_switched_controls) cond_turn_right = true;
						if (port_id == !cond_switched_controls) cond_turn_left = true;
					}
				}
			}

			cond_buffers_start[port_id] = i - last_semicolon;
			memcpy(cond_buffers[port_id], cond_buffers[port_id] + last_semicolon, cond_buffers_start[port_id]);
		}
	}
}

void cond_zero() {
	for (uint32_t i = 0; i < ARRAY_COUNT(cond_serial_ports); i++) {
		cond_zeros[i] = cond_means[i];
	}
}

void cond_set_threshold(uint32_t port_id) {
	cond_thresholds[port_id] = cond_means[port_id];
}

bool8_t is_key_down_or_cond(int key) {
	if (key == KEY_RIGHT) return IsKeyDown(key) || cond_right;
	else if (key == KEY_LEFT) return IsKeyDown(key) || cond_left;
	else if (key == KEY_UP) return IsKeyDown(key) || cond_turn_left || cond_turn_right;
	else return IsKeyDown(key);
}

static void aetris_main(int argc, char **argv) {
	if (!initialize_networking()) {
		CRASH("Network initalization");
	}

	screen_width = 1600;
	screen_height = 900;

	srand(time(NULL));

	memzero(&server_state);
	memzero(&client_state);

	server_state.socket = 0;
	client_state.socket = create_client_socket();
	if (client_state.socket == 0) {
		CRASH("Couldn't create client");
	}

	aetris_state = S_TITLE_SCREEN;

	cond_init();
	
	InitWindow(screen_width, screen_height, "Aetris");
	InitAudioDevice();
	Music background_music = LoadMusicStream("tetris.mp3");

 float pan = 0.0f;               // Default audio pan center [-1.0f..1.0f]
	SetMusicPan(background_music, pan);

	float volume = 0.3f;            // Default audio volume [0.0f..1.0f]
	SetMusicVolume(background_music, volume);

	PlayMusicStream(background_music);
	
	SetTargetFPS(60);

	bool8_t should_close = false;

	while (!should_close)  {
		mouse_x = GetMouseX();
		mouse_y = GetMouseY();
		
		UpdateMusicStream(background_music);

		cond_update();

		if (IsKeyDown(KEY_T)) {
			cond_zero();
		}

		if (IsKeyDown(KEY_L)) {
			cond_set_threshold(0);
		}
		
		if (IsKeyDown(KEY_R)) {
			cond_set_threshold(1);
		}

		if (IsKeyPressed(KEY_D)) {
			cond_debug_graph = !cond_debug_graph;
		}

		if (IsKeyPressed(KEY_S)) {
			cond_switch_controls();
		}
		
		switch (aetris_state) {
		case S_TITLE_SCREEN: {
			BeginDrawing();

			ClearBackground( (Color){ 0x18, 0x18, 0x18, 1 } );

#define TITLE_TEXT "AETRIS"
#define TITLE_TEXT_HEIGHT 50

			int text_width = MeasureText(TITLE_TEXT, TITLE_TEXT_HEIGHT);
			DrawText(TITLE_TEXT, screen_width / 2 - text_width / 2, screen_height / 12,
												TITLE_TEXT_HEIGHT, BLUE);

			if (menu_button("Create local server", 0)) {
				server_state.socket = create_server_socket(DEFAULT_SERVER_PORT);
				if (server_state.socket == 0) {
					CRASH("Couldn't create server");
				}

				server_state.speed = DEFAULT_SPEED;

				client_state.distant_server_addr.ip[0] = 127;
				client_state.distant_server_addr.ip[1] = 0;
				client_state.distant_server_addr.ip[2] = 0;
				client_state.distant_server_addr.ip[3] = 1;

				client_state.distant_server_addr.port = le_to_be_u16(DEFAULT_SERVER_PORT);

				aetris_state = S_PLAYING;
			}

			if (menu_button("Connect to server", 1)) aetris_state = S_CONNECT;

			if (menu_button("Quit game", 2)) should_close = true;

			EndDrawing();

			if (WindowShouldClose()) should_close = true;
		} break;
		case S_CONNECT: {
			if (argc == 3) {
				if (!parse_ipv4(STR(argv[1]), client_state.distant_server_addr.ip)) {
					CRASH("Wrong arguments!");
				}

				int64_t port;
				if (!string_to_int(STR(argv[2]), &port) || port < 0 || port >= (1 << 16)) {
					CRASH("Wrong arguments!");
				}

				client_state.distant_server_addr.port = le_to_be_u16(port);
				aetris_state = S_PLAYING;
			} else {
				CRASH("TODO!\n");
			}
		} break;
		case S_PLAYING: {
			{ // Client network handling
				if (!client_state.sent_hello) {
					client_send_packet(P_HELLO, 0, 0);
					client_state.sent_hello = true;
				}

				//  Conditioner input
				client_state.being_pressed[K_LEFT] = cond_left;
				client_state.being_pressed[K_RIGHT] = cond_right;
				
				// Handle user input
				for (uint32_t i = 0; i < ARRAY_COUNT(keys_of_interest); i++) {
					bool8_t will_send = false;

					if (i == K_UP || i == K_SPACE) {
						client_state.being_pressed[i] = false;
					}

					if (is_key_down_or_cond(keys_of_interest[i])) {
						if (client_state.until_next_press[i] == 0) {
							will_send = true;

							client_state.until_next_press[i] = client_state.being_pressed[i] ? KEY_PRESS_FREQ : UNTIL_HELD_DOWN;
							client_state.being_pressed[i] = true;
						}
					} else {
						client_state.being_pressed[i] = false;
					}

					if (will_send) {
						switch (i) {
						case K_LEFT: client_send_packet(P_MOVE_LEFT, 0, 0); break;
						case K_RIGHT: client_send_packet(P_MOVE_RIGHT, 0, 0); break;
						case K_UP: client_send_packet(P_MOVE_ROTATE, 0, 0); break;
						case K_SPACE: client_send_packet(P_MOVE_FALL, 0, 0); break;
						}
					}

					if (client_state.until_next_press[i] > 0) {
						client_state.until_next_press[i] -= 1;
					}
				}

				uint8_t packet[PACKET_MAX_SIZE];
				AeAddr src_addr;
				uint64_t bytes_count;

				if (receive_packet(client_state.socket, packet, sizeof packet,
																							&src_addr, &bytes_count) == R_READY &&
								address_equal(&src_addr, &client_state.distant_server_addr) &&
								bytes_count >= 5)
				{
					uint32_t packet_id;
					uint8_t packet_type;

					se_begin(packet, sizeof packet);

					se_read_u32(&packet_id);
					se_read_u8(&packet_type);

					switch (packet_type) {
					case P_PLAYERS: {
						se_read_u32(&client_state.players_count);

						uint8_t is_me;

						for (uint32_t i = 0; i < client_state.players_count; i++) {
							se_read_u8(&is_me);
							if (is_me) client_state.current_player_index = i;

							se_read_s32(&client_state.players[i].pos_x);
							se_read_s32(&client_state.players[i].pos_y);

							se_read_u8(&client_state.players[i].piece_id);
							se_read_u8(&client_state.players[i].piece_rot);
						}
					} break;
					case P_BOARD: {
						memcpy(client_state.board, packet + 5, sizeof client_state.board);

						if (!client_state.has_board) client_state.has_board = true;
					} break;
					}

					se_end(0);
				}
			}

			// Client Rendering
			BeginDrawing();

			ClearBackground( (Color){ 0x18, 0x18, 0x28, 1 } );

			uint32_t square_size = screen_height / BOARD_HEIGHT;

			if (!client_state.has_board || client_state.players_count == 0) {
				DrawText("Loading...", screen_width / 2 - 50, screen_height / 2, ITEMS_TEXT_HEIGHT, RED);
			} else {
				uint32_t x_offset = screen_width / 2 - (BOARD_WIDTH * square_size) / 2;

				DrawRectangle(x_offset, 0,
																		BOARD_WIDTH * square_size,
																		BOARD_HEIGHT * square_size,
																		BLACK);

				Color board_line_color = { 0x30, 0x30, 0x30, 0xff };

				for (uint32_t x = 1; x < BOARD_WIDTH; x++) {
					DrawLine(x_offset + x * square_size, 0,
														x_offset + x * square_size, BOARD_HEIGHT * square_size,
														board_line_color);
				}

				for (uint32_t y = 1; y < BOARD_HEIGHT; y++) {
					DrawLine(x_offset, y * square_size,
														x_offset + BOARD_WIDTH * square_size, y * square_size,
														board_line_color);
				}

				// Draw board
				for (uint32_t x = 0; x < BOARD_WIDTH; x++) {
					for (uint32_t y = 0; y < BOARD_HEIGHT; y++) {
						if (client_state.board[BOARD_AT(x, y)]) {
							uint8_t piece_id = client_state.board[BOARD_AT(x, y)] - 1;

							DrawRectangle(x_offset + x * square_size, y * square_size,
																					square_size, square_size, tetris_pieces_colors[piece_id]);
						}
					}
				}


				// Draw players
				for (uint32_t i = 0; i < client_state.players_count; i++) {
					uint32_t piece_id = client_state.players[i].piece_id;

					uint8_t *piece_mat = (tetris_pieces + piece_id * PIECE_SIZE);

					if (i == client_state.current_player_index) {
						// Draw around the piece if it's "me"
						for (int32_t x = 0; x < PIECE_WIDTH; x++) {
							for (int32_t y = 0; y < PIECE_HEIGHT; y++) {
								if (piece_mat[PIECE_AT(x, y)]) {
									Rectangle rec = piece_part_rectangle(&client_state.players[i], x, y, x_offset, square_size);

#define PIECE_SELECTION_THICKNESS 5
									DrawRectangleRoundedLinesEx(rec, 0.1f, 1, PIECE_SELECTION_THICKNESS, WHITE);
								}
							}
						}

						// Draw piece preview
						{
							Player old_player = client_state.players[i];
							while (player_can_move(client_state.players, client_state.players_count, client_state.board,
																														&client_state.players[i], 0, 1, false) == NO_COLLISION)
							{
								client_state.players[i].pos_y += 1;
							}

							for (int32_t x = 0; x < PIECE_WIDTH; x++) { // Draw the piece
								for (int32_t y = 0; y < PIECE_HEIGHT; y++) {
									if (piece_mat[PIECE_AT(x, y)]) {
										Rectangle rec = piece_part_rectangle(&client_state.players[i], x, y, x_offset, square_size);

										Color preview_color = tetris_pieces_colors[piece_id];
										preview_color.a = 125;
										DrawRectangleRec(rec, preview_color);
									}
								}
							}

							client_state.players[i] = old_player;
						}
					}

					for (int32_t x = 0; x < PIECE_WIDTH; x++) { // Draw the piece
						for (int32_t y = 0; y < PIECE_HEIGHT; y++) {
							if (piece_mat[PIECE_AT(x, y)]) {
								Rectangle rec = piece_part_rectangle(&client_state.players[i], x, y, x_offset, square_size);

								DrawRectangleRec(rec, tetris_pieces_colors[piece_id]);
							}
						}
					}
				}
			}


#define VAL_MAX 60000
#define VAL_MIN 0

#define SLIDING_MEAN_COUNT 10

			Color serial_ports_colors[ARRAY_COUNT(cond_serial_ports)] = {
				RED, GREEN
			};

			int32_t serial_ports_offset[ARRAY_COUNT(cond_serial_ports)] = {
				100, -300
			};
			
			if (cond_enabled && cond_debug_graph) {
				for (uint32_t port_id = 0; port_id < ARRAY_COUNT(cond_serial_ports); port_id++) {
					int zero_y = (cond_zeros[port_id] * screen_height) / VAL_MAX + serial_ports_offset[port_id];
					int threshold_y = (cond_thresholds[port_id] * screen_height) / VAL_MAX + serial_ports_offset[port_id];

					DrawLine(0, zero_y, screen_width, zero_y, serial_ports_colors[port_id]);
					DrawLine(0, threshold_y, screen_width, threshold_y, PURPLE);
					
					for (int64_t i = 0; i < COND_MAX_VALUES - 1; i++) {			
						int x1 = (i * screen_width) / COND_MAX_VALUES;
						int x2 = ((i + 1) * screen_width) / COND_MAX_VALUES;
						int y1 = (cond_values[port_id][i] * screen_height) / VAL_MAX + serial_ports_offset[port_id];
						int y2 = (cond_values[port_id][i + 1] * screen_height) / VAL_MAX + serial_ports_offset[port_id];

						DrawLine(x1, y1, x2, y2, serial_ports_colors[port_id]);
					}
				}
			}
			
			EndDrawing();

			if (server_state.socket) update_server();

			if (WindowShouldClose()) { // Close event handling
				if (server_state.socket) ae_socket_close(server_state.socket);

				client_send_packet(P_BYE, 0, 0);
				should_close = true;
			}
		} break;
		}
	}

	CloseWindow();
}
