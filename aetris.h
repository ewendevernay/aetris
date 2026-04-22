#pragma once

/* Useful stuff */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define false 0
#define true 1

typedef uint8_t bool8_t;

#define ARRAY_COUNT(x) (sizeof (x) / sizeof (x[0]))
#define memzero(x) memset(x, 0, sizeof (*x))

/* Networking abstract API layer */

typedef struct AeSock AeSock;

typedef struct {
	uint8_t ip[4];
	uint16_t port;
} AeAddr;

typedef enum {
	R_ERROR = 0,
	R_READY,
	R_WAIT
} NBResult;

static bool8_t initialize_networking();
static AeSock *create_server_socket(uint16_t port);
static AeSock *create_client_socket();
static bool8_t ae_socket_close(AeSock *socket);
static NBResult receive_packet(AeSock *socket, uint8_t *buffer, uint64_t buffer_size, AeAddr *out_src_addr, uint64_t *out_bytes_read);
static NBResult send_packet(AeSock *socket, AeAddr *dest_addr, uint8_t *buffer, uint64_t buffer_size, uint64_t *out_bytes_sent);

/* Miscalleanous abstract API layer */

static void *allocate_pages(uint64_t size);
static void ae_sleep(uint32_t secs);