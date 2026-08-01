#include "bytecode_format.h"
#include "bytecode_wire.h"

BC_FormatStatus bc_decode_header(const uint8_t *bytecode, uint32_t length,
                                 BC_FormatHeader *out) {
  BC_FormatHeader h = {0};
  if (!bytecode || length < 2u) {
    h.status = BC_FORMAT_TRUNCATED;
    goto done;
  }
  if (bytecode[0] == BC_V1_RESERVED_LOCALS &&
      bytecode[1] == BC_V1_RESERVED_PARAMS) {
    if (length < BC_V1_HEADER_SIZE) {
      h.status = BC_FORMAT_TRUNCATED;
      goto done;
    }
    if (bytecode[2] != BC_V1_MAGIC_S || bytecode[3] != BC_V1_MAGIC_B) {
      h.status = BC_FORMAT_INVALID;
      goto done;
    }
    h.version = bc_wire_load_u16(bytecode + 4);
    if (h.version != BC_V1_VERSION) {
      h.status = BC_FORMAT_UNSUPPORTED_VERSION;
      goto done;
    }
    h.locals = bytecode[6];
    h.params = bytecode[7];
    if (h.params > h.locals) {
      h.status = BC_FORMAT_INVALID;
      goto done;
    }
    h.instruction_offset = BC_V1_HEADER_SIZE;
    h.instructions = bytecode + BC_V1_HEADER_SIZE;
    h.legacy = false;
    h.status = BC_FORMAT_OK;
    goto done;
  }
  h.locals = bytecode[0]; h.params = bytecode[1];
  h.instruction_offset = 2u; h.instructions = bytecode + 2; h.legacy = true;
  h.status = h.params > h.locals ? BC_FORMAT_INVALID : BC_FORMAT_OK;
done:
  if (out) *out = h;
  return h.status;
}

void bc_encode_v1_header(uint8_t header[BC_V1_HEADER_SIZE], uint8_t locals,
                         uint8_t params) {
  header[0] = BC_V1_RESERVED_LOCALS;
  header[1] = BC_V1_RESERVED_PARAMS;
  header[2] = BC_V1_MAGIC_S;
  header[3] = BC_V1_MAGIC_B;
  bc_wire_store_u16(header + 4, BC_V1_VERSION);
  header[6] = locals;
  header[7] = params;
}
