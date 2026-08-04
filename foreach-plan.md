# FOREACH implementation plan

Working document for the `FOREACH` control structure. Untracked scratch, like
`pre-0.8.0-plan.md`. Self-contained: a fresh session should be able to execute
this without further design discussion.

## Feature

```
FOREACH <@local> IN <expression> DO <statements> ENDFOR;
```

The iterator is always a local. The sequence may be any expression whose value
is a list, including a code item that returns one. A non-list sequence executes
the body zero times.

## Decisions locked

| Decision | Resolution |
|---|---|
| Type guard | New **user-facing** `list.islist{value}` → bool, permanent pair `(5, 6)`. No hidden or compiler-only libcalls — everything in `src/libcall/` is language surface. |
| Iterator when sequence is not a list | `nil` |
| Iterator when list is empty | `nil` (same store, unconditionally, in the prologue) |
| `foreach` / `in` / `endfor` as keywords | Reserve them. The general keyword-shadows-layer-name fix is pinned separately in `pre-0.8.0-plan.md` and lands before 0.8.0. |

## Why there is no bytecode change

Bytecode v1's opcode ABI is frozen, so `FOREACH` cannot be an opcode. It
desugars in `lower.c` into existing opcodes.

**Unchanged:** `opcode_schema.def`, `emitbc.c`, `bytecode_verify.c`,
`runtime_decode.c`, `runtime_opcode.c`, `interpret.c`, `sdiss_core.c`,
`docs/bytecode.md`. Existing bytecode stays valid; new bytecode runs on the
current VM. `sdiss` renders a `FOREACH` as its constituent loop with the
libcalls named, so no disassembler work.

The whole runtime-side cost is one new libcall handler.

## The one ABI addition

```c
X("list", "islist",      5,  6, 1, lc_list_islist) \
```

`lc_list_islist` pops one value, pushes `true` iff `type == VALUE_list` and the
`.list` pointer is non-NULL, frees the argument, **never sets `error`**, always
returns `nextop`. Model it on `lc_list_length` in the same file.

Why it is needed: `list.length` on a non-list pushes `nil` *and* sets
`ERR_RUNTIME_INVALIDARGS` (`libcall_list.c:23-31`). Since `@i < nil` is already
false, the zero-iteration behaviour falls out for free — the guard exists purely
to keep `error` clean, which matters because `if error then` is a normal idiom.

Verified prerequisites: the verifier resolves libcall arity from the registry by
pair (`bytecode_verify.c:261`), so a new pair needs no verifier change; and
`value_move` frees the destination before overwriting (`value.c:308`), so
reusing hidden local slots cannot leak.

## Surface syntax

```ebnf
statement ::= "foreach" local "in" expression "do" statement-list "endfor"
```

Three new tokens: `TFOREACH`, `TIN`, `TENDFOR`. The grammar forces the iterator
to be a local, so a non-local iterator is a parse error, not a semantic one.

## AST

Two new node types, using the existing binary-node encoding rather than a new
struct (`AS_IF` earns its struct because of the elsif chain; a fixed 3-tuple
does not):

```
N_FOREACH      lhs = N_FOREACHSPEC     rhs = body (N_STMTLIST)
N_FOREACHSPEC  lhs = N_VALUE(V_LOCAL)  rhs = sequence expression
```

Both fall into `as_delete`'s generic binary case and `as_walk_internal`'s
generic tail. Add them to those case lists and to `nodename[]`
(`absyn.c:295`), which must stay in sync with the enum.

## Semantic analysis

New `case N_FOREACH` in `sem_walk`, in this order:

1. Walk the **sequence expression first**. This makes `foreach @x in @x do`
   legal when `@x` already holds a list, and correctly rejects it otherwise.
2. `sem_add_local` the iterator.
3. Register three hidden locals for the current nesting depth.
4. `loop_depth++`, walk body, `loop_depth--`, so `break`/`continue` are legal
   inside.

`SEM_CTX` gains a `foreach_depth` counter, reset alongside `loop_depth` in
`sem_check_locals_diag`.

## Lowering

For `foreach @x in SEQ do BODY endfor` at nesting depth *D*, with hidden locals
**S** (sequence), **I** (index), **N** (count), and iterator **X**:

```
        <SEQ>                  ; evaluated exactly once
        STORE_LOCAL  S
        PUSH_NIL
        STORE_LOCAL  X         ; iterator := nil, AFTER SEQ, never before
        PUSH_INT     0
        STORE_LOCAL  I
        LOAD_LOCAL   S
        LIBCALL      list.islist
        JUMP_IF_FALSE Lend     ; not a list -> zero iterations, error untouched
        LOAD_LOCAL   S
        LIBCALL      list.length
        STORE_LOCAL  N
Lcond:  LABEL
        LOAD_LOCAL   I
        LOAD_LOCAL   N
        LT
        JUMP_IF_FALSE Lend
        LOAD_LOCAL   S
        LOAD_LOCAL   I
        LIBCALL      list.get
        STORE_LOCAL  X
        <BODY>                 ; break -> Lend, continue -> Lnext
Lnext:  LABEL
        INC_LOCAL    I
        JUMP         Lcond
Lend:   LABEL
```

Three things that are easy to get wrong:

- **`continue` targets `Lnext`, not `Lcond`.** Copying the `N_WHILESTMT`
  pattern, which points `continue` at the condition, would skip the increment
  and hang. Save/restore `ctx->break_label` and `ctx->continue_label` around
  `BODY` exactly as `N_WHILESTMT` does.
- **The count is hoisted into `N`, not recomputed.** The list is captured in `S`
  and lists are immutable, so its length cannot change; recomputing would put a
  libcall in the inner loop for nothing.
- **The iterator is nil-stored after `SEQ` is evaluated**, so
  `foreach @x in @x do` still works.

Peak operand-stack growth is 2, well inside the verifier's 1024.

## Hidden locals

Named unlexably so they can never collide with source locals — the lexer stores
locals *with* the `@` and only produces `@[a-z_][a-z0-9_]*`:

```
@$foreach.<D>.seq
@$foreach.<D>.idx
@$foreach.<D>.len
```

Keyed by **nesting depth**, so sequential loops in one code item share slots and
only nesting consumes budget. `op_savelocal` → `value_move` frees the previous
occupant, so reuse releases the prior list.

**The one real coupling risk:** `semant` registers these names and `lower` looks
them up, so their numbering must agree exactly. Put the name construction in a
single shared helper that both call — do not spell the format string twice.
Depth is unambiguous because `FOREACH` is a statement and so cannot appear
inside the sequence expression.

Budget: 3 slots per nesting level against the 255 local limit. Deep nesting
would otherwise hit `ERR_COMP_TOOMANYLOCALS` with a hidden name in the message;
emit a dedicated diagnostic instead ("foreach nesting exceeds the local
budget").

## Normative semantics to document

1. `FOREACH` is a **statement**; it yields no value.
2. The sequence expression is evaluated **exactly once**, before the iterator is
   assigned. A code item in that position is called once, not once per element.
3. The iterator is assigned `nil` before the first iteration; after the loop it
   holds the last element visited, or `nil` if none were.
4. A non-list sequence executes the body zero times and **does not set `error`**.
5. Iteration is over an immutable snapshot: rebinding the source local inside
   the body does not affect the remaining iterations.
6. `break` exits; `continue` advances to the next element.
7. The iterator remains in scope after `ENDFOR` (the language has no block
   scoping).

## Files

**Compiler** — `lexer.l` (3 keywords) · `parser.y` (tokens, `%type`, one
production) · `absyn.h`/`absyn.c` (2 node types, delete, walk, `nodename[]`) ·
`semant.h`/`semant.c` (`foreach_depth`, `N_FOREACH` case) · `lower.c`
(`N_FOREACH` case) · one shared hidden-local naming helper.

**Libcall** — `libcall_list.h` (one row) · `libcall_list.c` (`lc_list_islist`) ·
`libcall_handlers.h`.

**Docs** — `language-reference.md` (reserved words `:36`, grammar `:89`,
evaluation order, semantics, limits note) · `libcalls.md` (`list.islist` row +
intro `:44`) · `lists.md` (iteration) · `concepts.md` if it covers loops ·
`tests/fixtures/conformance/README.md`.

**Untouched, deliberately** — everything under `src/bytecode/`, `src/runtime/`,
`src/itemstore/`, and `docs/bytecode.md`.

## Tests

- **`test_libcall_list.c`** — `list.islist` across all seven value types;
  confirm it never touches `error`.
- **`test_semant.c`** — iterator defined after the loop; `break`/`continue`
  legal inside; undefined local in the sequence rejected; deep nesting hits the
  local budget with the new diagnostic.
- **Compiler golden** — bytecode for a simple loop; nested loops use distinct
  slots; sequential loops reuse slots.
- **Interpreter semantics** — empty list · non-list **asserting `error` is
  untouched** · `nil` elements iterate correctly · `break` · `continue` ·
  nesting · **sequence evaluated once** (code item incrementing a counter) ·
  snapshot semantics under rebinding · iterator value after zero iterations.
- **Conformance** — positive additions to `positive-core.src`; new negative
  fixture `negative/parser-foreach-nonlocal-iterator.src`
  (`foreach foo in #[1] do`). Register both in `tests/shared/test_fixture_policy.c`
  with `SOT:`/`regen:` metadata and in `conformance/README.md`.
- **Fuzz** — add a `FOREACH` sample to `tests/fuzz/corpus/scomp`.

## Validation

Language change, so per `AGENTS.md`: `make test` → `make test-warnings` →
`make test-release` → `./ci/gate_sanitizers_fuzz.sh`. The sanitizer pass matters
more than usual: hidden-slot reuse is exactly where a missed free would show up.

## Commit breakdown

1. `list.islist` + tests + `libcalls.md`. Independently useful and reviewable.
2. Lexer, parser, AST.
3. Semant + lower + compiler tests.
4. Interpreter semantics tests + conformance fixtures.
5. Language reference and remaining docs.

## Process

Implementation runs through the `AGENTS.md` Luna ladder: Luna (Medium) for
attempts 1-5 with root review of every diff, then Sol for at most two, then
root. Test execution goes to `luna-test`, which reports raw output only.

Related pins are recorded in `pre-0.8.0-plan.md` under
"Pinned before the 0.8.0 freeze": keywords shadowing item layer names, and
item arguments being evaluated with side effects.
