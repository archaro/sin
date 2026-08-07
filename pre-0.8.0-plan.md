  ## Essential work before the freeze

  1. Introduce bytecode format v1

  Use a self-identifying header for every code block, whether it is a standalone
  scomp object or embedded in an itemstore. For example:

  magic       4 bytes
  version     u16 little-endian
  locals      u8
  parameters  u8
  instructions...

  The bytecode version should be 1, independent of Sinistra’s 0.8.0 release
  number.

  Because current itemstore v2 files may contain unversioned bytecode, detection
  must not be heuristic. Two reasonable approaches are:

  - Use a new itemstore version which requires versioned code blocks.
  - Choose a bytecode magic prefix that cannot be a valid legacy locals/
    parameters pair—for example, beginning 00 FF, since the verifier rejects
    parameter counts greater than local counts.

  I prefer the second approach if we want to avoid creating itemstore v3 solely
  for this purpose.

  The compiler, verifier, interpreter, sin, sdiss, sys.compile, sys.paramcount,
  fixtures and documentation must all stop indexing the first two bytes directly
  and use a shared header decoder.

  2. Define a portable wire representation

  Most multi-byte operands currently use the host’s representation. The
  documentation consequently limits portability to machines with matching byte
  order and integer representation (docs/bytecode.md:29).

  Before freezing, bytecode v1 should specify:

  - little-endian fixed-width integers;
  - two’s-complement signed integers;
  - IEEE 754 binary64 float payloads;
  - exact signed jump-offset interpretation;
  - explicit string-length and collection-size limits.

  All emitter, verifier, runtime decoder and disassembler reads should use the
  same encoding helpers.

  3. Stabilise libcall identifiers

  This is probably the most serious hidden ABI problem. Bytecode currently
  contains a one-byte libcall token (docs/bytecode.md:117), but that token is
  simply the libcall’s position in LIBCALL_LIST (src/libcall/
  libcall_registry.c:274). Inserting or reordering entries can silently make old
  bytecode call a different function.

  Before the freeze, I would replace that token with the existing explicit
  (library index, call index) pair from src/libcall/libcall_list.h:7. Those
  identifiers should then be permanently assigned:

  - never renumber an existing call;
  - never reuse a retired identifier;
  - reject unknown identifiers during verification;
  - allow implementation dispatch tables to remain dense internally.

  4. Freeze the opcode ABI

  For each opcode, formally freeze:

  - encoded byte;
  - operand layout;
  - stack input and output;
  - control-flow behaviour;
  - error behaviour;
  - evaluation order;
  - termination behaviour.

  Removed opcodes must become reserved rather than being reassigned. The
  existing opcode schema is a good source of truth, but it needs compatibility
  rules and tests around it.

  5. Add bytecode conversion to sconv

  Currently sconv converts the itemstore container but copies code bytes
  unchanged. Its test explicitly requires byte-for-byte preservation (tests/
  core/test_sconv.c:52), and the itemstore writer treats bytecode as an opaque
  payload (src/itemstore/item_persist.c:352).

  That should become a separate bytecode migration subsystem:

  probe version
  → verify using that version’s decoder
  → decode instructions
  → emit latest version
  → verify emitted bytecode

  sconv should invoke it for every code item. Conversion should remain atomic:
  one unconvertible code item must prevent publication of a partially converted
  store.

  From 0.8.0 onward, retain fixtures for every released bytecode version and
  test:

  - each supported version → latest;
  - semantic equivalence before and after conversion;
  - malformed historical bytecode;
  - conversion idempotence;
  - unknown versions;
  - itemstores containing multiple code versions, if those are permitted.

  I would promise conversion from bytecode v1 onward. Current unversioned
  bytecode can receive a one-time legacy converter without becoming part of the
  permanent compatibility contract.

  ## Language work still worth deciding before 0.8.0

  The implementation is now reasonably broad, but the normative language
  definition is not ready to freeze. The documentation roadmap still describes
  the formal reference as future work (docs/documentation-roadmap.md:34).

  Before 0.8.0, I would require:

  - A canonical language reference covering lexical rules, grammar, precedence,
    associativity and evaluation order.

  - Exact semantics for truthiness, equality, coercion and arithmetic across
    every value type.

  - Exact code-item call, fallthrough, RETURN, parameter and side-effect
    behaviour.

  - Exact list and item-reference ownership/value semantics.
  - Documented implementation limits and defined behaviour when limits are
    exceeded.

  - Positive and negative conformance fixtures derived from that reference.

  There is also a language-design decision worth making now:

  - Whether lists need native iteration syntax. While list.get is
    sufficient, a future foreach would change the frozen grammar.

  In short: the major remaining work is not another expression feature. It is
  establishing a genuine bytecode ABI—especially portable encoding and stable
  libcall IDs—then making the language documentation and conformance suite
  authoritative. No files were changed and no tests were necessary for this
  review.

  ### Item arguments are evaluated, with side effects

  `expr : item args` builds an N_CALL even when `args` is empty, which lowers
  to opcode `F`, the shared fetch-or-call primitive. A bare item anywhere in
  expression position therefore fetches a value item or *executes* a code item.
  That applies uniformly: `str.len{foo.bar}`, `list.length{foo.bar}`,
  `list.islist{foo.bar}` and `@x = foo.bar` all run `foo.bar` if it is code.

  The language already allows side-effect-free inspection of an item's kind:
  `&foo.bar` builds a reference without evaluating, and `sys.itemtype` accepts
  a string or itemref and reports `"code"`/`"value"` without running anything.
  What has no side-effect-free form is obtaining an item's *value*, since
  `sys.fetch` on a code item schedules the call rather than returning a stored
  payload.

  This is a deliberate design choice.  Include a normative paragraph in the
  evaluation-order section of the language reference making the evaluation
  point explicit.

