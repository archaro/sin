# Itemstore operations

This page describes the live itemstore and its operational boundaries. The
[itemstore file-format reference](itemstore-format.md) owns the wire tables;
this page explains the in-memory model, staging, and ownership around them.

## Names and tree topology

An `ITEMSTORE_t` owns one root and its complete descendant tree. Dot-separated
paths passed to the direct tree APIs are relative to the supplied root/item;
their ordinary first layer must not be empty. Non-root layers are ASCII
letters, digits, or underscore, are at most 32 bytes, and are canonical lower
case. These APIs canonicalize case-insensitively before lookup or mutation. A
leading-dot path is a separate relative-name syntax: callers first resolve it
with `item_path_canonicalize_relative()` (or the runtime's equivalent
canonicalization) using a context item, then pass the resulting ordinary path
to tree lookup/mutation. The root label is not folded. The context's ancestor
depth and full path count toward the depth (eight non-root layers) and
full-name limits. Empty, trailing, repeated, or otherwise malformed layers
are rejected before the tree changes.

Each item's child container is opaque to callers. It combines a hash table for
lookup with an insertion-ordered array for deterministic traversal and save
order. The hash representation may resize; the order array is the observable
iteration order. Child links, values, bytecode, and item pointers are owned by
the store. Roots and other item pointers are borrowed and become invalid when
their store or containing subtree is destroyed.

## Mutations, pins, and revisions

`item_set_value()` and `item_set_code()` create missing intermediate value
items, or replace the target payload. Creation/replacement is transactional:
allocation, name, payload-limit, alias, and pin checks happen before a
successful result. On `CREATED` or `REPLACED`, `result.item` is the borrowed
resulting leaf and the store owns the supplied payload. On every failure the
tree and revisions are unchanged and the caller retains the payload, except a
rejected alias that was already owned by the target. `item_delete()` removes a
whole subtree and returns `DELETED` with a null item; absent valid names return
`NOT_FOUND`.

Execution frames pin code items while they run. Replacement rejects a pinned
target; deletion rejects a pinned target or any pinned descendant. Pins are
transient and are not serialized. A topology revision changes for creation and
deletion, invalidating both positive and negative lookup-cache entries. Store
destruction invalidates all borrowed item pointers and
cache state; no revision is observed after the store is gone. A payload
revision changes for successful value/code replacement without changing
topology, so cached item pointers remain valid. Revisions have epochs/exhaustion
flags to prevent token reuse during a store lifetime; hit/miss counters are
per-store and invalid names do not touch them.

## Protected errors

The root-relative `error` namespace (`error` and its descendants) is reserved
for runtime publication. Runtime mutation helpers reject writes there and set a
diagnostic instead. `set_error_item()` and `set_compiler_error_item()` build a
separate staging tree, validate all destinations and pin state, then transfer
all fields in one publication step. The structured schema is:

```text
error          integer error number
error.msg      formatted human-readable message
error.item     current item name, or nil
error.code     stable compiler code, or nil
error.stage    compiler phase, or nil
error.file     source name, or nil
error.line     one-based line, or nil/0
error.column   one-based column, or nil/0
error.excerpt  source-line excerpt, or nil
```

Publication preserves an existing complete destination set when staging or
allocation fails; incomplete destinations are normalized to nil where needed.
Clearing errors normalizes the same fields. Error fields are ordinary value
items in memory but their namespace and publication semantics are protected
runtime contracts.

## Load, save, conversion, and durability

Loading constructs a detached tree while reading and validating each record,
then returns a store only after the complete stream succeeds. Truncation,
unsupported tags/versions, limits, allocation failure, trailing data, and (when
requested) strict bytecode verification discard the partial tree and return
`NULL`. Strict validation is a load-time option; executable runtime paths still
perform mandatory verification before execution. Save performs an admissibility
preflight, serializes v2 to an exclusive temporary file beside the destination,
flushes/closes it, and publishes by replacement. The no-replace API uses
exclusive no-replace publication. `full` additionally synchronizes the file and
containing directory where supported; `fast` skips those sync calls. Failures before
publication leave the old destination in place where possible; a directory
sync failure can occur after replacement and therefore reports failure without
claiming the destination is unchanged. Mutations become durable only after a
successful save boundary.

`sconv` is the explicit migration tool. Its conversion path loads the legacy
version, stages converted code payloads with a separate work budget, and saves
the new destination only after all conversion and validation work succeeds.
It does not turn normal runtime loading into an auto-converter.

Source sidecars are independent files under `srcroot/<canonical path>/source.sin`.
Only code items may have sidecars. Save/read helpers borrow the item and source
for the call and return/consume no itemstore ownership; sidecar I/O can fail
independently of itemstore save durability. Sidecar source is therefore useful
provenance, not the persisted code payload or an atomic part of the itemstore
publication.

## Maintenance map

Tree, canonicalization, revisions, cache, and mutation ownership are in
`src/itemstore/item_tree.c`, `item_hash.c`, `item_registry.c`, and `item.h`.
Load/save staging and conversion orchestration are in `item_persist.c`, with
version codecs in `item_persist_v1.c` and `item_persist_v2.c`; sidecars are in
`item_source_persist.c`, and structured errors in `item_error.c`.
Corresponding tests are `tests/core/test_item_cache.c`,
`test_itemstore_io.c`, `test_sin_itemstore_policy.c`, `test_sconv.c`, and the
itemstore adapters under `tests/rewrite/`. Keep malformed/boundary cases and
failure-atomicity assertions next to the changed boundary.
