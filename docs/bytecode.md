# Sinistra Bytecode Reference

This document is the canonical human-readable reference for Sinistra bytecode.
The IR opcode metadata in `src/compiler/ir/opcode_schema.def` is the source of
truth for opcode symbols, operand kinds, size policies, validators, and runtime
handler requirements.

Bytecode is an internal, release-local format. Compatibility with bytecode
emitted by an earlier prerelease, build, or release is not guaranteed. If a
tool rejects bytecode produced by another version, recompile it from Sinistra
source; there is no legacy-opcode compatibility layer or automatic bytecode
migration.

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
  local index for a local variable layer, or another item assembly. Nested item
  assemblies are limited to eight levels.
* **Embedded code** (`B`) optionally begins with a parameter block: `P`, zero or
  more parameter names as `u16 length + bytes`, then a terminating zero `u16`
  length. The parameter block is followed by the mandatory source block encoded
  as `u16 source_length + source bytes`.

## Opcode reference

All opcodes are single-byte symbols. Some opcodes are followed by immediate
arguments as described below.

| Opcode | IR opcode(s) | Immediate bytes | Description |
| --- | --- | --- | --- |
| `a` | `IR_OP_ADD` | none | Pop the top two values. Add integers, treating `nil` as integer `0`; add floats with int-to-float promotion when needed; concatenate strings; and push the result. Integer overflow pushes `nil`. Invalid mixed types push `nil`. |
| `b` | `IR_OP_PUSH_BOOL` | `u8 value` | Push a boolean value; non-zero is true and zero is false. |
| `c` | `IR_OP_STORE_LOCAL` | `u8 local_index` | Store the top stack value into the addressed local slot and pop it. |
| `d` | `IR_OP_DIV` | none | Pop two numeric values, divide the previous value by the top value, and push the result. Integer-only divide by zero substitutes zero; integer overflow, including `INT64_MIN / -1`, pushes `nil`. Float division follows IEEE 754 after int-to-float promotion. Invalid operands produce `nil` or the integer-zero result for non-float invalid division. |
| `e` | `IR_OP_LOAD_LOCAL` | `u8 local_index` | Push a copy of the addressed local value. |
| `f` | `IR_OP_INC_LOCAL` | `u8 local_index` | Increment an integer local; report an error for non-integers. |
| `g` | `IR_OP_DEC_LOCAL` | `u8 local_index` | Decrement an integer local; report an error for non-integers. |
| `h` | `IR_OP_HALT` | none | Stop the current code item. If returning from a call, resume the caller with the return value; otherwise terminate interpretation. |
| `j` | `IR_OP_JUMP` | `i16 relative_offset` | Unconditionally jump by the signed relative offset. |
| `k` | `IR_OP_JUMP_IF_FALSE` | `i16 relative_offset` | Pop the top value. Jump by the signed relative offset when it is falsey; otherwise continue with the next instruction. |
| `l` | `IR_OP_PUSH_STRING` | `u16 length`, bytes | Push a string literal. |
| `m` | `IR_OP_MUL` | none | Pop two numeric values, multiply them, and push the result. Integer-only multiplication returns an integer unless it would overflow, in which case it pushes `nil`; any float operand promotes the operation to binary64. Invalid operands produce `nil`. |
| `n` | `IR_OP_NEG` | none | Pop an integer or float, negate it, and push it back. Unary negation of integer `INT64_MIN` pushes `nil`. Invalid operands are left to the VM error path. |
| `o` | `IR_OP_EQ` | none | Pop two values, compare equality, and push a boolean. |
| `p` | `IR_OP_PUSH_INT` | `i64 value` | Push a 64-bit integer. |
| `P` | `IR_OP_PUSH_FLOAT` | 8-byte binary64 payload | Push an IEEE 754 binary64 float whose raw bits are stored in the immediate payload. |
| `q` | `IR_OP_NEQ` | none | Pop two values, compare inequality, and push a boolean. |
| `r` | `IR_OP_LT` | none | Pop two values, compare less-than, and push a boolean. |
| `s` | `IR_OP_SUB` | none | Pop two numeric values, subtract the top value from the previous value, and push the result. Integer-only subtraction returns an integer unless it would overflow, in which case it pushes `nil`; any float operand promotes the operation to binary64. Invalid operands produce `nil`. |
| `t` | `IR_OP_GT` | none | Pop two values, compare greater-than, and push a boolean. |
| `u` | `IR_OP_LE` | none | Pop two values, compare less-than-or-equal, and push a boolean. |
| `v` | `IR_OP_GE` | none | Pop two values, compare greater-than-or-equal, and push a boolean. |
| `w` | `IR_OP_DISCARD` | none | Pop and free the top stack value. The compiler emits this after expression statements whose value is not the code item's result. |
| `x` | `IR_OP_NOT` | none | Pop the top value, apply logical not, and push the boolean result. |
| `y` | `IR_OP_AND` | none | Pop the top two values, apply logical and, and push the boolean result. |
| `z` | `IR_OP_OR` | none | Pop the top two values, apply logical or, and push the boolean result. |
| `B` | `IR_OP_ITEM_SAVE_CODE` | optional params, then source block | Compile embedded source code and assign the compiled code item to the item name on top of the stack. On success, clear the error item. Malformed embedded payloads set `ERR_RUNTIME_BYTECODE`; invalid target item names set `ERR_RUNTIME_INVALIDITEM`; source compilation failures set the compiler error item. |
| `C` | `IR_OP_ITEM_SAVE` | none | Pop an item name and value, then save the value into the item. |
| `D` | `IR_OP_ITEM_PUSH_DEREF` | deref payload | Inside item assembly, append a dereferenced layer name. The payload identifies the dereference source, such as `V` plus a local index. |
| `E` | `IR_OP_ITEM_END` | none | End item assembly. Evaluate the assembled item name and push the resulting name, or `nil` if the name is invalid. |
| `F` | `IR_OP_ITEM_DEREF`, `IR_OP_CALL` | `u16 argument_count` | Fetch item contents or call a code item; see below. |
| `I` | `IR_OP_ITEM_BEGIN` | item layers until `E` | Begin absolute item-name assembly. |
| `L` | `IR_OP_ITEM_PUSH_LAYER` | `u8 length`, bytes | Inside item assembly, append a literal layer name. |
| `M` | `IR_OP_LIBCALL_TOKEN` | `u8 token` | Dispatch a prevalidated library-call registry token. |
| `R` | `IR_OP_ITEM_BEGIN_REL` | item layers until `E` | Begin relative item-name assembly using the current item as context. |
| `V` | `IR_OP_ITEM_PUSH_DEREF_LOCAL` | `u8 local_index` | Inside a `D` dereference payload, turn the addressed local value into a layer name. |


## Code item result semantics

At `IR_OP_HALT`, the interpreter returns the value above the current frame. If
there is no value above the frame, the result is `nil`. For nested code-item
calls, the callee result is pushed back onto the caller's stack; for top-level
execution, it is returned by the interpreter.

The compiler makes result-producing source code explicit with `IR_OP_DISCARD`.
Only a final top-level expression statement is left as the code item's result.
Earlier expression statements are evaluated for side effects and then compiled
with `DISCARD`, so `1; 2;` returns `2`, not `1`. Assignments, `if` statements,
and `while` statements do not themselves produce a result, so `@x = 7;`,
`if true then 7; endif;`, and `while ... do 7; endwhile;` all return `nil`
unless followed by a final top-level expression statement. Expression statements
inside `if` branches and loop bodies are discarded as part of those statements;
they do not become the enclosing code item's result.

Libcalls follow the same rule. A final libcall expression, such as
`sys.exists{"foo"};`, returns the libcall result. A non-final libcall expression,
such as `sys.exists{"foo"}; 5;`, still performs its side effects, but its return
value is discarded and the code item returns `5`. `sys.compile{source}` also
follows this rule: the compiled source can create or update items even when the
`sys.compile` return value is discarded by a following expression statement.

## Numeric edge cases

Integer arithmetic is signed 64-bit arithmetic with checked overflow. `IR_OP_ADD` is the only arithmetic opcode that treats `nil` as integer `0`; `IR_OP_SUB`, `IR_OP_MUL`, `IR_OP_DIV`, and `IR_OP_NEG` do not treat `nil` as numeric. Overflow in
`IR_OP_ADD`, `IR_OP_SUB`, `IR_OP_MUL`, `IR_OP_DIV`, and integer `IR_OP_NEG`
pushes `nil`; it does not wrap, saturate, or trap. Examples include
`INT64_MAX + 1`, `INT64_MIN - 1`, `3037000500 * 3037000500`,
`INT64_MIN / -1`, and `-INT64_MIN`. Integer division by zero pushes integer `0`. Float operations, including division
by zero, use IEEE 754 binary64 results after any integer-to-float promotion.

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

By default, discarding arguments in these cases is silent by design. This lets
running worlds tolerate live code updates while callers and callees are being
brought back into agreement after a parameter-list change. When the runtime is
started with `--strict-runtime-contracts`, the `F` primitive still performs the
same stack normalization and return-value behavior, but each discarded argument
caused by an over-arity code-item call, invalid item name, or missing target item
sets `error` to `ERR_RUNTIME_INVALIDARGS` and writes a diagnostic to `error.msg`.
This option is separate from `--strict-validation`, which validates bytecode
structure before execution, and it has a runtime cost because it performs extra
contract checks. For example, an over-arity call such as `add{1, 2, 3}` still
returns the same value that `add{1, 2}` would return, and a missing-target call
such as `missing.item{1}` still returns `nil`; strict runtime contracts
additionally set `ERR_RUNTIME_INVALIDARGS` and explain that an argument was
discarded.

The IR schema distinguishes the two producers of `F`:

* `IR_OP_ITEM_DEREF` is a plain item dereference and has no IR operand; the
  emitter writes an encoded argument count of zero.
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
5. Treat changed bytecode as release-local and document that affected programs
   must be recompiled from source.
