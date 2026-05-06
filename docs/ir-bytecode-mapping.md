# IR to Bytecode Mapping

This document defines how `IR_OP` instructions map to the bytecode stream consumed by the interpreter.

## Bytecode container layout

The runtime expects:

- Byte `0`: number of locals
- Byte `1`: number of parameters
- Byte `2..`: opcode stream
- Terminal opcode: `'h'` (HALT)

## Direct opcode mappings

| IR op | Opcode | Operand encoding | Nominal stack effect |
|---|---:|---|---:|
| `IR_OP_PUSH_INT` | `'p'` | `int64` (8 bytes, native-endian) | +1 |
| `IR_OP_PUSH_STR` | `'l'` | `u16 length` + UTF-8 bytes | +1 |
| `IR_OP_LOAD_LOCAL` | `'e'` | `u8 local` | +1 |
| `IR_OP_STORE_LOCAL` | `'c'` | `u8 local` | -1 |
| `IR_OP_INC_LOCAL` | `'f'` | `u8 local` | 0 |
| `IR_OP_DEC_LOCAL` | `'g'` | `u8 local` | 0 |
| `IR_OP_ADD` | `'a'` | none | -1 |
| `IR_OP_SUB` | `'s'` | none | -1 |
| `IR_OP_MUL` | `'m'` | none | -1 |
| `IR_OP_DIV` | `'d'` | none | -1 |
| `IR_OP_EQ` | `'o'` | none | -1 |
| `IR_OP_NE` | `'q'` | none | -1 |
| `IR_OP_LT` | `'r'` | none | -1 |
| `IR_OP_LE` | `'u'` | none | -1 |
| `IR_OP_GT` | `'t'` | none | -1 |
| `IR_OP_GE` | `'v'` | none | -1 |
| `IR_OP_NOT` | `'x'` | none | 0 |
| `IR_OP_AND` | `'y'` | none | -1 |
| `IR_OP_OR` | `'z'` | none | -1 |
| `IR_OP_EXISTS` | `'X'` | none | 0 |
| `IR_OP_DELETE` | `'W'` | none | -1 |
| `IR_OP_NTHNAME` | `'Y'` | none | -1 |
| `IR_OP_ROOTNAME` | `'Z'` | none | 0 |
| `IR_OP_JUMP` | `'j'` | signed `i16` relative offset | 0 |
| `IR_OP_JUMPFALSE` | `'k'` | signed `i16` relative offset | -1 |
| `IR_OP_HALT` | `'h'` | none | 0 |

## Composite/special mappings

- `IR_OP_BUILD_ITEM`: emits `'I'` and the item assembly byte grammar consumed by `op_assembleitem`.
- `IR_OP_DEREF`: encoded as part of item-assembly subgrammar (not a standalone VM opcode).
- `IR_OP_LIBCALL`: emits `'A'` followed by library and function IDs (`u8`, `u8`).

## Unresolved mappings (must be defined before enabling full lowering)

- `IR_OP_CALL`: map to item fetch/execute (`'F'`) or split into a more explicit IR call protocol.
- `IR_OP_RETURN`: either lower to jump-to-epilogue or keep function-end semantics based on terminal `'h'` and top-of-stack return.

## Emitter invariants

1. Jump offsets are relative to the byte immediately after the opcode byte (`nextop` model).
2. Jump operand width is signed 16-bit; overflow is a compile-time error.
3. Local indices are `u8`; overflow is a compile-time error.
4. Emitter must always terminate stream with `'h'`.
