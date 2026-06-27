# Sinistra Bytecode Reference

This document is the canonical human-readable reference for Sinistra bytecode.
The IR opcode metadata in `src/compiler/ir/opcode_schema.def` is the source of
truth for opcode symbols, operand kinds, size policies, validators, and runtime
handler requirements.

## Code block layout

Every compiled code item starts with a two-byte header followed by the
instruction stream.

| Byte offset | Size | Field | Description |
| --- | ---: | --- | --- |
| 0 | 1 | locals count | Unsigned count of local slots used by this code block, including parameter slots. |
| 1 | 1 | parameter count | Unsigned count of leading local slots populated from call arguments. |
| 2 | to end | instruction stream | Encoded bytecode instructions for the interpreter to execute. |

The interpreter enters a code item by allocating `locals count - parameter count`
additional local slots, recording the local and parameter counts, and beginning
execution at byte offset 2.

## Encoding conventions

Unless an opcode description says otherwise, numeric immediates are written in
the platform representation used by the C emitter and interpreter. The compiler
centralizes fixed-width writes through helpers for `uint8_t`, `uint16_t`,
`int16_t`, `int64_t`, and raw `uint64_t` payloads; the interpreter uses matching
fixed-width read helpers. Multi-byte helpers copy bytes with `memcpy` rather
than pointer casts, so unaligned instruction streams are safe while preserving
the existing byte-for-byte format. Current encodings are therefore little-endian
on supported little-endian builds, and persisted bytecode is portable only across
platforms with the same integer widths, two's-complement signed representation,
and byte order assumptions.

* **Opcode bytes** are single-byte character symbols. For example, `p` is
  `IR_OP_PUSH_INT` and `h` is `IR_OP_HALT`.
* **One-byte immediates** (`u8`) follow the opcode directly and are used for
  local indexes, boolean values, libcall tokens, and item layer lengths.
* **Two-byte immediates** (`u16` or signed `i16`) follow the opcode directly.
  Strings and embedded source blocks use unsigned 16-bit lengths. Jumps use a
  signed 16-bit relative offset.
* **Eight-byte immediates** are used by `p` / `IR_OP_PUSH_INT` and `P` /
  `IR_OP_PUSH_FLOAT`. Integers are encoded as `i64`. Floats are encoded as raw
  IEEE 754 binary64 payload bits copied through the emitter and interpreter as
  an eight-byte `uint64_t` blob; the payload is not numerically converted during
  bytecode I/O.
* **Strings** are encoded as a 16-bit unsigned byte length followed by exactly
  that many bytes. A trailing NUL is not stored in bytecode.
* **Jumps** (`j`, `k`) encode a signed 16-bit offset measured from the first
  byte after the jump opcode (the start of the two-byte offset field). A taken
  branch sets the instruction pointer to `offset_field_start + offset`; a
  not-taken conditional branch skips the two offset bytes.
* **Item names** are assembled between `I` or `R` and `E`. Literal layers use
  `L`, a one-byte unsigned length, and the layer-name bytes. Dereferenced layers
  use `D` followed by the nested dereference kind, currently `V` plus a one-byte
  local index for a local variable layer, or another item assembly.
* **Embedded code** (`B`) optionally begins with a parameter block: `P`, zero or
  more parameter names as `u16 length + bytes`, then a terminating zero `u16`
  length. The parameter block is followed by the mandatory source block encoded
  as `u16 source_length + source bytes`.

## Opcode reference

All opcodes are single-byte symbols. Some opcodes are followed by immediate
arguments as described below.

| Opcode | IR opcode(s) | Immediate bytes | Description |
| --- | --- | --- | --- |
| `a` | `IR_OP_ADD` | none | Pop the top two values. Add integers, add floats with int-to-float promotion when needed, concatenate strings, and push the result. Invalid mixed types push `nil`. |
| `b` | `IR_OP_PUSH_BOOL` | `u8 value` | Push a boolean value; non-zero is true and zero is false. |
| `c` | `IR_OP_STORE_LOCAL` | `u8 local_index` | Store the top stack value into the addressed local slot and pop it. |
| `d` | `IR_OP_DIV` | none | Pop two numeric values, divide the previous value by the top value, and push the result. Integer-only divide by zero substitutes zero; float division follows IEEE 754 after int-to-float promotion. Invalid operands produce `nil` or the historical integer-zero result for non-float invalid division. |
| `e` | `IR_OP_LOAD_LOCAL` | `u8 local_index` | Push a copy of the addressed local value. |
| `f` | `IR_OP_INC_LOCAL` | `u8 local_index` | Increment an integer local; report an error for non-integers. |
| `g` | `IR_OP_DEC_LOCAL` | `u8 local_index` | Decrement an integer local; report an error for non-integers. |
| `h` | `IR_OP_HALT` | none | Stop the current code item. If returning from a call, resume the caller with the return value; otherwise terminate interpretation. |
| `j` | `IR_OP_JUMP` | `i16 relative_offset` | Unconditionally jump by the signed relative offset. |
| `k` | `IR_OP_JUMP_IF_FALSE` | `i16 relative_offset` | Pop the top value. Jump by the signed relative offset when it is falsey; otherwise continue with the next instruction. |
| `l` | `IR_OP_PUSH_STRING` | `u16 length`, bytes | Push a string literal. |
| `m` | `IR_OP_MUL` | none | Pop two numeric values, multiply them, and push the result. Integer-only multiplication returns an integer; any float operand promotes the operation to binary64. Invalid operands produce `nil`. |
| `n` | `IR_OP_NEG` | none | Pop an integer or float, negate it, and push it back. Invalid operands are left to the VM error path. |
| `o` | `IR_OP_EQ` | none | Pop two values, compare equality, and push a boolean. |
| `p` | `IR_OP_PUSH_INT` | `i64 value` | Push a 64-bit integer. |
| `P` | `IR_OP_PUSH_FLOAT` | 8-byte binary64 payload | Push an IEEE 754 binary64 float whose raw bits are stored in the immediate payload. |
| `q` | `IR_OP_NEQ` | none | Pop two values, compare inequality, and push a boolean. |
| `r` | `IR_OP_LT` | none | Pop two values, compare less-than, and push a boolean. |
| `s` | `IR_OP_SUB` | none | Pop two numeric values, subtract the top value from the previous value, and push the result. Integer-only subtraction returns an integer; any float operand promotes the operation to binary64. Invalid operands produce `nil`. |
| `t` | `IR_OP_GT` | none | Pop two values, compare greater-than, and push a boolean. |
| `u` | `IR_OP_LE` | none | Pop two values, compare less-than-or-equal, and push a boolean. |
| `v` | `IR_OP_GE` | none | Pop two values, compare greater-than-or-equal, and push a boolean. |
| `x` | `IR_OP_NOT` | none | Pop the top value, apply logical not, and push the boolean result. |
| `y` | `IR_OP_AND` | none | Pop the top two values, apply logical and, and push the boolean result. |
| `z` | `IR_OP_OR` | none | Pop the top two values, apply logical or, and push the boolean result. |
| `B` | `IR_OP_ITEM_SAVE_CODE` | optional params, then source block | Compile embedded source code and assign the compiled code item to the item name on top of the stack. On success, clear the error item; on failure, assign `nil` and store the compiler error message. |
| `C` | `IR_OP_ITEM_SAVE` | none | Pop an item name and value, then save the value into the item. |
| `D` | `IR_OP_ITEM_PUSH_DEREF` | deref payload | Inside item assembly, append a dereferenced layer name. The payload identifies the dereference source, such as `V` plus a local index. |
| `E` | `IR_OP_ITEM_END` | none | End item assembly. Evaluate the assembled item name and push the resulting name, or `nil` if the name is invalid. |
| `F` | `IR_OP_ITEM_DEREF`, `IR_OP_CALL` | shared; see below | Fetch item contents or call a code item. |
| `I` | `IR_OP_ITEM_BEGIN` | item layers until `E` | Begin absolute item-name assembly. |
| `L` | `IR_OP_ITEM_PUSH_LAYER` | `u8 length`, bytes | Inside item assembly, append a literal layer name. |
| `M` | `IR_OP_LIBCALL_TOKEN` | `u8 token` | Dispatch a prevalidated library-call registry token. |
| `R` | `IR_OP_ITEM_BEGIN_REL` | item layers until `E` | Begin relative item-name assembly using the current item as context. |
| `V` | deref payload byte | `u8 local_index` | Inside a `D` dereference payload, turn the addressed local value into a layer name. |
| `W` | `IR_OP_DELETE` | none | Pop an item name. Delete the item when it exists; push nothing. |
| `X` | `IR_OP_EXISTS` | none | Pop an item name and push true if it resolves to an item, otherwise false. |
| `Y` | `IR_OP_NTHNAME` | none | Pop an index and item name. Push the name of the indexed child, or `nil` if no such child exists. |
| `Z` | `IR_OP_ROOTNAME` | none | Pop an index. Push the name of the indexed root child item, or `nil` if no such child exists. |

## Shared `F` opcode behavior

`IR_OP_ITEM_DEREF` and `IR_OP_CALL` intentionally share the encoded opcode byte
`F`. Both operations execute the same VM primitive: fetch the item named by the
string on top of the stack.

The shared primitive canonicalizes the item name in the current item context,
then behaves as follows:

* If the target is a value item, it pushes a copy of the value.
* If the target is a code item, it normalizes the supplied argument count to the
  callee parameter count by discarding extra arguments or pushing `nil` for
  missing arguments, saves the caller continuation, and transfers execution to
  the callee.
* If the target does not exist or the item name is invalid, it discards supplied
  arguments and pushes `nil`.

The IR schema distinguishes the two producers of `F`:

* `IR_OP_ITEM_DEREF` is a plain item dereference and has no IR operand in the
  schema.
* `IR_OP_CALL` carries the call arity as an unsigned 16-bit immediate after `F`.

A plain dereference is semantically a zero-argument fetch. Keep the schema,
emitter, interpreter, and this reference aligned whenever changing the `F`
encoding or arity handling.

## Maintaining this reference

`src/compiler/ir/opcode_schema.def` is the source of truth for IR opcode
metadata. When any row in that file changes, update this document in the same
change set:

1. Update the opcode table for symbol, IR name, immediate layout, size, or
   behavior changes.
2. Update the encoding conventions when a row introduces or changes an operand
   kind, size policy, validator, jump convention, string format, item-name
   format, or embedded-code format.
3. Update the shared-opcode notes when rows intentionally share an encoded
   symbol, especially `F`.
4. Run the opcode schema, emitter, and compiler pipeline tests that cover the
   changed rows, then include those commands in the change summary.
5. If compatibility matters for existing persisted bytecode, document any
   migration or versioning implications here.
