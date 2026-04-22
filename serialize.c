/*
Simple library for data serialization. Only supports machines that are
little endian and use two's complement representation for their signed
integers.

- TODO: Floating point serialization.
*/

#pragma once
#include <inttypes.h>
#include <string.h>

static uint8_t *se_buffer;
static uint64_t se_buffer_size;
static uint64_t se_offset;

static void se_begin(uint8_t *buffer, uint64_t size) {
	se_buffer = buffer;
	se_buffer_size = size;

	se_offset = 0;
}

static bool8_t se_end(uint64_t *out_size) {
	if (se_offset >= se_buffer_size) return false;

	if (out_size) *out_size = se_offset;

	se_buffer = 0;
	se_buffer_size = 0;
	se_offset = 0;

	return true;
}

static void se_write_bytes(uint8_t *bytes, uint64_t count) {
	uint8_t *dst = se_buffer + se_offset;
	se_offset += count;

	if (se_offset < se_buffer_size) memcpy(dst, bytes, count);
}

static void se_write_u8(uint8_t x) {
	uint8_t *dst = se_buffer + se_offset;
	se_offset++;

	if (se_offset < se_buffer_size) *dst = x;
}

static void se_write_u16(uint16_t x) {
	uint16_t *dst = (uint16_t*)(se_buffer + se_offset);
	se_offset += sizeof x;

	if (se_offset < se_buffer_size) *dst = x;
}

static void se_write_u32(uint32_t x) {
	uint32_t *dst = (uint32_t*)(se_buffer + se_offset);
	se_offset += sizeof x;

	if (se_offset < se_buffer_size) *dst = x;
}

static void se_write_u64(uint64_t x) {
	uint64_t *dst = (uint64_t*)(se_buffer + se_offset);
	se_offset += sizeof x;

	if (se_offset < se_buffer_size) *dst = x;
}

static void se_write_s8(int8_t x) {
	int8_t *dst = (int8_t*)(se_buffer + se_offset);
	se_offset += sizeof x;

	if (se_offset < se_buffer_size) *dst = x;
}

static void se_write_s16(int16_t x) {
	int16_t *dst = (int16_t*)(se_buffer + se_offset);
	se_offset += sizeof x;

	if (se_offset < se_buffer_size) *dst = x;
}

static void se_write_s32(int32_t x) {
	int32_t *dst = (int32_t*)(se_buffer + se_offset);
	se_offset += sizeof x;

	if (se_offset < se_buffer_size) *dst = x;
}

static void se_write_s64(int64_t x) {
	int64_t *dst = (int64_t*)(se_buffer + se_offset);
	se_offset += sizeof x;

	if (se_offset < se_buffer_size) *dst = x;
}

static bool8_t se_read_bytes(uint8_t *out_bytes, uint64_t size) {
	if (se_offset + size >= se_buffer_size) return false;

	memcpy(out_bytes, se_buffer + se_offset, size);
	se_offset += size;

	return true;
}

static bool8_t se_read_u8(uint8_t *out) {
	if (se_offset + sizeof (*out) >= se_buffer_size) return false;

	*out = *(uint8_t*)(se_buffer + se_offset);
	se_offset += sizeof (*out);

	return true;
}

static bool8_t se_read_u16(uint16_t *out) {
	if (se_offset + sizeof (*out) >= se_buffer_size) return false;

	*out = *(uint16_t*)(se_buffer + se_offset);
	se_offset += sizeof (*out);

	return true;
}

static bool8_t se_read_u32(uint32_t *out) {
	if (se_offset + sizeof (*out) >= se_buffer_size) return false;

	*out = *(uint32_t*)(se_buffer + se_offset);
	se_offset += sizeof (*out);

	return true;
}

static bool8_t se_read_u64(uint64_t *out) {
	if (se_offset + sizeof (*out) >= se_buffer_size) return false;

	*out = *(uint64_t*)(se_buffer + se_offset);
	se_offset += sizeof (*out);

	return true;
}

static bool8_t se_read_s8(int8_t *out) {
	if (se_offset + sizeof (*out) >= se_buffer_size) return false;

	*out = *(int8_t*)(se_buffer + se_offset);
	se_offset += sizeof (*out);

	return true;
}

static bool8_t se_read_s16(int16_t *out) {
	if (se_offset + sizeof (*out) >= se_buffer_size) return false;

	*out = *(int16_t*)(se_buffer + se_offset);
	se_offset += sizeof (*out);

	return true;
}

static bool8_t se_read_s32(int32_t *out) {
	if (se_offset + sizeof (*out) >= se_buffer_size) return false;

	*out = *(int32_t*)(se_buffer + se_offset);
	se_offset += sizeof (*out);

	return true;
}

static bool8_t se_read_s64(int64_t *out) {
	if (se_offset + sizeof (*out) >= se_buffer_size) return false;

	*out = *(int64_t*)(se_buffer + se_offset);
	se_offset += sizeof (*out);

	return true;
}