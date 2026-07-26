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
| Name length | 32 bytes per layer |
| Children of one item | 250 |
| String payload | 65,535 bytes (`SIN_MAX_STRING_BYTES`) |
| Bytecode payload | 64 MiB (`64 * 1024 * 1024` bytes) |

Non-root names must contain between 1 and 32 bytes and consist only of ASCII
letters, digits, and underscore. Names may not contain an embedded NUL.
Sibling names must be unique. The root name is also limited to 32 bytes and
may not contain an embedded NUL, but it is not subject to the non-root character
set restriction.

The in-memory item APIs use these same limits before changing a tree. A path
passed to `item_set_value`, `item_set_code`, `find_item`,
`find_item_cached`, or `item_delete` is relative to the supplied item pointer;
the supplied item's ancestor depth counts toward the depth limit. Its complete
non-root path must fit within 263 bytes including separators. Invalid paths are
rejected without creating intermediate items, changing the itemstore
topology/payload revisions, or updating cache hit/miss counters.

The loader aborts the entire load on any validation, allocation, truncation, or
I/O failure. A partially constructed tree is destroyed and `load_itemstore`
returns `NULL`.

Mutations through the in-memory item APIs take effect immediately in the loaded
tree, but are not durable until `save_itemstore` completes. Normal safe
shutdown, including `sin --loadonly`, saves the itemstore; `sys.abort` skips that
save. A failed save reports failure and does not claim durability. A code-item
source copy written under `srcroot` is a separate best-effort file write and is
not covered by the itemstore durability mode.

## Save and replacement behavior

`save_itemstore` creates an exclusive, collision-resistant temporary file
beside the destination, writes the v1 stream to it, flushes and closes it, and
then renames it over the destination. A pre-existing temporary file is never
opened with truncation or replaced. The `--itemstore-durability` startup option
controls synchronization:

- `full` is the default. On POSIX systems, it calls `fsync` on the temporary
  file after `fflush` and before close and replacement, then calls `fsync` on
  the containing directory after the rename. This is the strongest contract
  available from this implementation, subject to the filesystem's own crash
  semantics; it is not a guarantee against hardware or operating-system
  failure.
- `fast` skips both synchronization calls, but still flushes, closes, and
  renames the temporary file. This can substantially reduce save latency on
  physical storage, but an operating-system crash or power loss can lose or
  corrupt the latest save.

On Windows builds, the synchronization hooks are no-ops, so `full` still
flushes, closes, and replaces the file but does not provide POSIX-style file or
directory `fsync` durability. Other platforms should be treated similarly
unless their build supplies equivalent synchronization semantics.

In either mode, serialization, temporary-file creation, flush, file sync,
close, rename, or any required directory sync failure returns `false`. A
failure before rename removes the temporary file where possible and leaves the
existing destination in place. A directory-sync failure occurs after rename:
the function still returns `false`, but the destination may already contain the
new data. The function returns `true` only after replacement and all required
durability steps succeed.

## Versioning

Readers accept only the version they implement. A future incompatible layout
must use a new header version and update this document. Version 1 does not
provide migration from the earlier unversioned raw layout.

The itemstore version describes the container's wire structure, not the
compatibility of bytecode payloads stored in code items. Prerelease bytecode is
release-local and may be rejected by a newer runtime even when the surrounding
itemstore remains structurally valid version 1. Retain source and recompile code
items for the current build; the runtime does not migrate embedded bytecode.
