#include "bytecode_wire.h"

uint16_t bc_wire_load_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t bc_wire_load_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t bc_wire_load_u64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; i++) value |= (uint64_t)p[i] << (i * 8u);
  return value;
}

int16_t bc_wire_load_i16(const uint8_t *p) {
  return bc_wire_i16_from_bits(bc_wire_load_u16(p));
}

int16_t bc_wire_i16_from_bits(uint16_t raw) {
  if (raw <= INT16_MAX) return (int16_t)raw;
  if (raw == UINT16_C(0x8000)) return INT16_MIN;
  return (int16_t)-((int16_t)(UINT16_MAX - raw) + 1);
}

int64_t bc_wire_load_i64(const uint8_t *p) {
  return bc_wire_i64_from_bits(bc_wire_load_u64(p));
}

int64_t bc_wire_i64_from_bits(uint64_t raw) {
  if (raw <= INT64_MAX) return (int64_t)raw;
  if (raw == UINT64_C(0x8000000000000000)) return INT64_MIN;
  return -(int64_t)(UINT64_MAX - raw + UINT64_C(1));
}

void bc_wire_store_u16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

void bc_wire_store_u32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8u));
}

void bc_wire_store_u64(uint8_t *p, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(value >> (i * 8u));
}

void bc_wire_store_i16(uint8_t *p, int16_t value) {
  bc_wire_store_u16(p, (uint16_t)value);
}

void bc_wire_store_i64(uint8_t *p, int64_t value) {
  bc_wire_store_u64(p, (uint64_t)value);
}
