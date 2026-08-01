#pragma once

#include <stdint.h>

uint16_t bc_wire_load_u16(const uint8_t *p);
uint32_t bc_wire_load_u32(const uint8_t *p);
uint64_t bc_wire_load_u64(const uint8_t *p);
int16_t bc_wire_load_i16(const uint8_t *p);
int64_t bc_wire_load_i64(const uint8_t *p);
int16_t bc_wire_i16_from_bits(uint16_t raw);
int64_t bc_wire_i64_from_bits(uint64_t raw);
void bc_wire_store_u16(uint8_t *p, uint16_t value);
void bc_wire_store_u32(uint8_t *p, uint32_t value);
void bc_wire_store_u64(uint8_t *p, uint64_t value);
void bc_wire_store_i16(uint8_t *p, int16_t value);
void bc_wire_store_i64(uint8_t *p, int64_t value);
