#pragma once

#include <inttypes.h>

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define false 0
#define true 1

typedef uint8_t bool8_t;

typedef struct {
	uint8_t *data;
	uint64_t data_size;

	uint64_t offset;
	uint32_t alignment;
} Pool;

static void pool_init(Pool *pool, uint8_t *data, uint64_t data_size) {
	pool->data = data;
	pool->data_size = data_size;
	pool->offset = 0;

	pool->alignment = 8; // Could be changed to something else
}


static void *pool_alloc(Pool *pool, uint64_t size) {
	uintptr_t curr_ptr = (uintptr_t)(pool->data + pool->offset);

	if (curr_ptr % pool->alignment != 0) {
		curr_ptr += pool->alignment - (curr_ptr % pool->alignment);
	}

	pool->offset = (uint8_t*)(curr_ptr) - pool->data;

	if (pool->offset + size >= pool->data_size) return 0;

	uint8_t *res = pool->data + pool->offset;
	pool->offset += size;

	return res;
}

static void pool_clear(Pool *pool) {
	pool->offset = 0;
}

static uint64_t pool_checkpoint(Pool *pool) {
	return pool->offset;
}

static void pool_set_checkpoint(Pool *pool, uint64_t checkpoint) {
	pool->offset = checkpoint;
}

static Pool pool_subpool(Pool *pool, uint64_t size) {
	if (pool->offset + size >= pool->data_size) return (Pool) {0};

	Pool subpool;

	subpool.data = pool->data + pool->offset;
	subpool.data_size = size;
	subpool.offset = 0;
	subpool.alignment = pool->alignment;

	return subpool;
}

typedef struct {
	char *data;
	uint64_t count;
} String;

#define STR_L(x) ((String){ x, (sizeof x) - 1 })
#define STR(x) ((String){ x, strlen(x) })

bool8_t string_to_int(String string, int64_t *out_int) {
	*out_int = 0;
	uint64_t base = 1;
	int8_t sign = 1;

	if (string.count == 0) {
		return false;
	}

	if (string.count > 1 && string.data[0] == '-') {
		sign = -1;
		string.data++;
		string.count--;
	}

	if (string.count > 18) {		/* No overflow */
		return false;
	}

	for (int64_t i = string.count - 1; i >= 0; i--) {
		if (string.data[i] < '0' || string.data[i] > '9') {
			return false;
		}

		*out_int += base * (string.data[i] - '0');
		base *= 10;
	}

	*out_int *= sign;
	return true;
}

String *string_split(Pool *pool, String string, char delimeter, uint32_t *out_count) {
	uint32_t delimeters_count = 0;
	for (uint64_t i = 0; i < string.count; i++) {
		if (string.data[i] == delimeter) {
			delimeters_count++;
		}

		while (i < string.count && string.data[i] == delimeter) {
			i++;
		}
	}

	*out_count = delimeters_count + 1;
	delimeters_count = 0;

	String *spliced = pool_alloc(pool, sizeof (String) * (*out_count));

	spliced[0].data = string.data;

	uint64_t last_i = 0, i;
	for (i = 0; i < string.count; i++) {
		if (string.data[i] == delimeter) {
			spliced[delimeters_count].count = i - last_i;

			while (i < string.count && string.data[i] == delimeter) {
				i++;
			}

			last_i = i;
			spliced[delimeters_count + 1].data = string.data + i;
			delimeters_count++;
		}

	}

	spliced[delimeters_count].count = string.count - last_i;

	return spliced;
}
