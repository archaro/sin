# Sinistra Itemstore File Format

This document is the canonical reference for the on-disk itemstore format.
The current format version is `1`. Compatibility is guaranteed only for
versioned itemstores beginning with v1.

Itemstore v1 was stabilised at Sinistra release 0.5.

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
| 8 | 2 | Format version | Little-endian `uint16`, currently `1` |
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
record. The writer emits children in the item model's ordered-array order, and
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

Other value-kind tags and boolean bytes greater than `1` are invalid. String
payloads do not include a terminator. Sinistra strings are C-string based, so
writers should not place embedded NUL bytes in string payloads.

### Code item payload

A code item contains a four-byte `uint32` bytecode length followed by exactly
that many bytecode bytes. The bytecode itself is defined in
[`bytecode.md`](bytecode.md).

## Validation limits

The v1 reader and writer enforce these limits:

| Property | Limit |
| --- | ---: |
| Item depth | Root is depth 0; maximum record depth is 8 |
| Name length | 32 bytes |
| Children of one item | 250 |
| String payload | 65,535 bytes (`SIN_MAX_STRING_BYTES`) |
| Bytecode payload | 64 MiB (`64 * 1024 * 1024` bytes) |

Non-root names must contain between 1 and 32 bytes and consist only of ASCII
letters, digits, and underscore. Names may not contain an embedded NUL.
Sibling names must be unique. The root name is also limited to 32 bytes and
may not contain an embedded NUL, but it is not subject to the non-root character
set restriction.

The loader aborts the entire load on any validation, allocation, truncation, or
I/O failure. A partially constructed tree is destroyed and `load_itemstore`
returns `NULL`.

## Save and replacement behavior

`save_itemstore` writes a temporary file beside the destination, flushes and
closes it, and then renames it over the destination. The
`--itemstore-durability` startup option controls synchronization:

- `full` is the default. On POSIX systems, it calls `fsync` on the temporary
  file after `fflush` and before close and replacement.
- `fast` skips `fsync`, but still flushes, closes, and renames the temporary
  file. This can substantially reduce save latency on physical storage, but an
  operating-system crash or power loss can lose or corrupt the latest save.

In either mode, if serialization or a reported file operation fails, the
temporary file is removed where possible and the existing destination is left
in place. The function returns `true` only after replacement succeeds.

## Versioning

Readers accept only the version they implement. A future incompatible layout
must use a new header version and update this document. Version 1 does not
provide migration from the earlier unversioned raw layout.
