# Sinistra Itemstore File Format

This document is the canonical reference for the on-disk itemstore format.
The current runtime format version is `2`. The normal runtime loader rejects v1,
which should be converted using `sconv`.

Itemstore v1 was stabilised at Sinistra release 0.5. It was superseded by v2 in
release 0.7.1; v2 retains all v1 scalar, code, and record bytes and adds lists
and item-reference payloads.

V2 list payloads are a `uint32` element count followed by recursively encoded
values. Item references are a `uint16` byte length followed by a canonical
root-relative dot-separated path without a leading dot. Lists are serialized
by value; pointer identity and persistent-vector internals are never written.

## Encoding conventions

- Every integer field has a fixed width and is encoded little-endian.
- Lengths count bytes, not characters.
- Names, strings, and bytecode are stored without an added trailing NUL.
- Item and value kinds use the stable wire tags defined below. C enum values,
  structure layout, padding, pointers, and native `sizeof` values are not part
  of the format.
- Signed integer values use an eight-byte `int64` payload.
- Floating-point values use the unchanged eight-byte IEEE 754 binary64 bit
  pattern. They are not converted to or from decimal during itemstore I/O.

The file contains one header followed by exactly one recursive root record.
Any bytes after the root record make the file invalid.

## File header

| Offset | Size | Field | Required value |
| ---: | ---: | --- | --- |
| 0 | 8 | Magic | `53 49 4e 49 54 45 4d 00` (`SINITEM` followed by NUL) |
| 8 | 2 | Format version | Little-endian `uint16`, currently `2` |
| 10 | variable | Root item | One item record as described below |

A short header, incorrect magic, or unsupported version is rejected before
the root record is read.

## Item record

Every item, including the root, uses this recursive structure:

| Order | Size | Field |
| ---: | ---: | --- |
| 1 | 1 | Name length (`uint8`) |
| 2 | name length | Name bytes |
| 3 | 1 | Item-kind tag (`uint8`) |
| 4 | variable | Payload selected by the item-kind tag |
| 5 | 4 | Child count (`uint32`) |
| 6 | variable | Exactly `child count` consecutive item records |

Children appear recursively and are associated with the immediately enclosing
record. The writer emits children in the item model's insertion order, and
the loader reconstructs that order. No offsets, alignment padding, or record
terminators are stored.

### Item-kind tags

| Tag | Kind | Payload |
| ---: | --- | --- |
| 1 | Value item | Value-kind tag followed by its value payload |
| 2 | Code item | Bytecode length and bytecode bytes |

Other item-kind tags are invalid.

### Value item payloads

A value item begins with a one-byte value-kind tag:

| Tag | Kind | Payload after tag |
| ---: | --- | --- |
| 0 | Integer | Eight-byte `int64` |
| 1 | Float | Eight-byte IEEE 754 binary64 bit pattern |
| 2 | String | Four-byte `uint32` byte length, then that many bytes |
| 3 | Nil | No payload |
| 4 | Boolean | One byte: `0` for false or `1` for true |
| 5 | List | Four-byte element count, then recursively encoded values |
| 6 | Item reference | Two-byte path length, then canonical root-relative path bytes |

Other value-kind tags and boolean bytes greater than `1` are invalid. String
payloads do not include a terminator. Sinistra strings are C-string based, so
writers should not place embedded NUL bytes in string payloads.

V2 adds value tags `5` (list) and `6` (item reference). Lists may contain at
most 1,048,576 elements and nest at most 64 levels. A file-wide aggregate
budget of 1,048,576 list elements applies across all records. References must
be 1..`MAX_ITEM_NAME - 1` bytes, contain at most `ITEM_MAX_DEPTH` valid layers,
and contain no empty, leading-dot, trailing-dot, or embedded-NUL layer.

### Code item payload

A code item contains a four-byte `uint32` bytecode length followed by exactly
that many bytecode bytes. The bytecode itself is defined in
[`bytecode.md`](bytecode.md).

## Validation limits

The v2 reader and writer enforce these limits (v1 uses the scalar subset and
is retained only for internal conversion decoding):

| Property | Limit |
| --- | ---: |
| Item depth | Root is depth 0; maximum record depth is 8 |
| Name length | 32 bytes per layer |
| Children of one item | 250 |
| String payload | 65,535 bytes (`SIN_MAX_STRING_BYTES`) |
| Bytecode payload | 64 MiB (`64 * 1024 * 1024` bytes) |
| Records in one file (root included) | 65,536 |
| Cumulative requested decode heap bytes | 256 MiB (`256 * 1024 * 1024` bytes) |

Strict bytecode verification uses a separate 16 MiB analysis-memory budget.
Instruction metadata is stored per decoded top-level instruction and boundary
tracking is bit-packed, so verification memory remains bounded for payloads at
the bytecode limit. Exceeding the budget is rejected deterministically.

Non-root names must contain between 1 and 32 bytes and consist only of ASCII
letters, digits, and underscore. Names may not contain an embedded NUL.
Non-root layers are stored in canonical ASCII lower case. v1 and v2 readers
reject non-canonical mixed-case layers atomically; the diagnostic includes the
original and expected spelling. v2 readers also reject non-canonical
item-reference paths. Sibling names must be unique. The root name is also
limited to 32 bytes and
may not contain an embedded NUL, but it is not subject to the non-root character
set restriction.

The same layer and path limits are enforced by the live itemstore APIs; see
[Itemstore Operations](itemstore.md). The wire reader additionally enforces
stream-level record, aggregate-list, bytecode, and decode-budget limits while
loading.

The record and cumulative decode-heap limits apply to every v2 load and to the
v1 conversion reader. A record is reserved before its payload is decoded, and
requested allocation sizes are charged cumulatively, including temporary list
and conversion-path storage; released temporary allocations are not refunded.
Conversion staging has a separate 256 MiB work budget. Budget failures reject
the complete load or conversion, and save performs the same admissibility
preflight before publishing a destination.

`itemstore_load()` (or `itemstore_load_with_options()` when strict bytecode
validation is requested) aborts the entire load: malformed, truncated, or
budget-exceeding streams are invalid.

## Publication and Durability

The writer serializes a complete v2 stream to an exclusive temporary file
beside the destination and publishes it by replacement only after serialization
and flush/close succeed. `full` durability additionally synchronizes the
temporary file and containing directory where supported; `fast` omits those
synchronization calls.

Failures before replacement leave the previous destination intact where
possible. A required directory-sync failure occurs after replacement and
therefore reports failure even though the new file may already be visible. See
[Itemstore Operations](itemstore.md) for the operational save lifecycle.

## Versioning

The normal runtime reader accepts only itemstore version 2, and the writer
always emits version 2. The frozen version 1 decoder is retained solely for the
explicit `sconv` migration path; normal runtime loading never auto-converts an
older store. A future incompatible itemstore layout must use a new header
version and update this document.

Itemstore and bytecode versions are independent. An itemstore code record
contains bytecode payload bytes but does not assign their bytecode version.
Runtime loading may verify those bytes but does not rewrite them. `sconv` may
migrate supported embedded bytecode while explicitly converting an older store;
bytecode compatibility rules are defined in
[the bytecode reference](bytecode.md).
