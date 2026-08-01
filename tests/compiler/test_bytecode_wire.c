#include <stdint.h>
#include <string.h>

#include "bytecode_wire.h"
#include "bytecode_verify.h"
#include "sdiss_core.h"
#include "runtime_decode.h"
#include "test_assert.h"

void test_bytecode_wire_boundary_vectors(void) {
  uint8_t bytes[8];
  const uint8_t u16_expected[] = {0x34, 0x12};
  bc_wire_store_u16(bytes, UINT16_C(0x1234));
  ASSERT_TRUE(memcmp(bytes, u16_expected, sizeof u16_expected) == 0);
  ASSERT_EQ_INT(UINT16_C(0x1234), bc_wire_load_u16(u16_expected));

  const uint8_t u32_expected[] = {0x78, 0x56, 0x34, 0x12};
  bc_wire_store_u32(bytes, UINT32_C(0x12345678));
  ASSERT_TRUE(memcmp(bytes, u32_expected, sizeof u32_expected) == 0);
  ASSERT_EQ_INT(UINT32_C(0x12345678), bc_wire_load_u32(u32_expected));

  const uint8_t u64_expected[] = {0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01};
  bc_wire_store_u64(bytes, UINT64_C(0x0123456789abcdef));
  ASSERT_TRUE(memcmp(bytes, u64_expected, sizeof u64_expected) == 0);
  ASSERT_EQ_INT(UINT64_C(0x0123456789abcdef), bc_wire_load_u64(u64_expected));

  const int16_t i16_values[] = {INT16_MIN, -1, 0, INT16_MAX};
  const uint8_t i16_bytes[][2] = {{0x00, 0x80}, {0xff, 0xff}, {0x00, 0x00}, {0xff, 0x7f}};
  for (size_t i = 0; i < 4; i++) {
    bc_wire_store_i16(bytes, i16_values[i]);
    ASSERT_TRUE(memcmp(bytes, i16_bytes[i], 2) == 0);
    ASSERT_EQ_INT(i16_values[i], bc_wire_load_i16(i16_bytes[i]));
  }

  const int64_t i64_values[] = {INT64_MIN, -1, 0, INT64_MAX};
  const uint8_t i64_bytes[][8] = {
      {0, 0, 0, 0, 0, 0, 0, 0x80}, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
      {0, 0, 0, 0, 0, 0, 0, 0}, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}};
  for (size_t i = 0; i < 4; i++) {
    bc_wire_store_i64(bytes, i64_values[i]);
    ASSERT_TRUE(memcmp(bytes, i64_bytes[i], 8) == 0);
    ASSERT_EQ_INT(i64_values[i], bc_wire_load_i64(i64_bytes[i]));
  }
}

static void append_disassembly(void *ctx, const char *data, size_t len) {
  char *out = ctx;
  size_t used = strlen(out);
  memcpy(out + used, data, len);
  out[used + len] = '\0';
}

void test_bytecode_wire_subsystems_agree(void) {
  const uint8_t bytes[] = {0, 0, 'p', 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 'h'};
  BC_VerifyResult verified = bc_verify_bytecode(bytes, sizeof bytes, "wire", NULL);
  ASSERT_EQ_INT(BC_VERIFY_OK, verified.status);

  RuntimeDecoder decoder;
  runtime_decoder_init(&decoder, (uint8_t *)bytes + 2, (uint8_t *)bytes + sizeof bytes);
  int64_t value = 0;
  RuntimeDecodeStatus status = bc_read_i64(&decoder, (uint8_t *)bytes + 3, &value, "wire");
  ASSERT_TRUE(runtime_decode_status_ok(status));
  ASSERT_EQ_INT(-1, value);

  char disassembly[256] = {0};
  SDissResult shown = sdiss_disassemble_bytes(bytes, sizeof bytes, NULL,
                                              append_disassembly, disassembly);
  ASSERT_EQ_INT(BC_VERIFY_OK, shown.status);
  ASSERT_TRUE(strstr(disassembly, "INTEGER -1") != NULL);
}
