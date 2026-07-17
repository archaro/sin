# Sinistra Tool Reference

This page documents the command-line tools built by the top-level `Makefile`:
`sin`, `scomp`, and `sdiss`.

## `scomp`

`scomp` compiles Sinistra source text to Sinistra bytecode/object data. It is
the tool to use when turning a `.sin` source file into an object file that can be
loaded by `sin` or inspected by `sdiss`.

Usage:

```sh
scomp <input file> <output file>
scomp -i <input file> -o <output file> [options]
```

Options:

| Option | Description |
| --- | --- |
| `-h`, `--help` | Print help and exit successfully. |
| `--version` | Print the compiler version and exit successfully. |
| `-i <file>`, `--input <file>` | Read source from `<file>`. Use `-` to read from standard input. |
| `-o <file>`, `--output <file>` | Write bytecode to `<file>`. Use `-` to write to standard output. At normal log level, progress messages are suppressed when writing to standard output so the bytecode stream is not mixed with status text. |
| `-q`, `--quiet` | Suppress progress/status messages. |
| `-v`, `--verbose` | Print verbose progress and diagnostic trace messages. |

On compilation failure, `scomp` exits non-zero and prints a structured compiler
diagnostic including stage, file, line, column, stable diagnostic code, error number, and a source excerpt when available.

## `sdiss`

`sdiss` disassembles Sinistra bytecode/object data into a textual instruction
listing. It validates the bytecode while disassembling and exits non-zero if the
input is malformed enough to abort disassembly.

Usage:

```sh
sdiss -o <object file> [options]
```

Options:

| Option | Description |
| --- | --- |
| `-h`, `--help` | Print help and exit successfully. |
| `--version` | Print the disassembler version and exit successfully. |
| `-o <file>`, `--object <file>` | Read bytecode/object data from `<file>`. This option is required. |
| `--raw` | Include raw bytes for each disassembled instruction. |
| `--no-header` | Skip the locals/parameters header output. |
| `-q`, `--quiet` | Suppress progress/status messages. |
| `-v`, `--verbose` | Print verbose progress and diagnostic trace messages. |

`sdiss` does not accept positional object-file arguments; use `-o` or
`--object`. Disassembler progress messages are normal output and are suppressed
by `--quiet`.

## `sin`

`sin` is the runtime/game executable. It loads an itemstore, executes a compiled
object, and, unless `--loadonly` is used, starts the libuv network loop and
runs the configured input handler.

Usage:

```sh
sin -o <object file> [options]
```

Options:

| Option | Description |
| --- | --- |
| `--loadonly` | Load and execute the object file, then shut down. This is useful for initializing or updating items without running the network game loop. |
| `-h`, `--help` | Print help and exit successfully. |
| `--version` | Print the runtime version and exit successfully. |
| `-i <file>`, `--itemstore <file>` | Load the itemstore from `<file>`. If the file does not exist, a new itemstore is created and saved under that name. If omitted, `items.dat` is used. Existing itemstores that fail to load are not replaced. |
| `-d <mode>`, `--itemstore-durability <mode>` | Select itemstore replacement durability. `full` is the default and synchronizes itemstore data before replacement. `fast` skips that synchronization but still flushes, closes, and renames the temporary file. |
| `-l[<file>]`, `--log[=<file>]` | Redirect log output. If no filename is supplied, `sin` is used. The runtime writes `<file>.log` for standard output messages and `<file>.err` for error output. For the short form, a following non-option argument is also accepted as the log basename. |
| `-n <item>`, `--input <item>` | Use `<item>` as the input-handler code item instead of `input`. This option must appear after `-i`/`--itemstore`, and the named item must already exist as a code item. The runtime derives `<item>.line` and `<item>.text` for network input details. |
| `-o <file>`, `--object <file>` | Read the compiled bootstrap object from `<file>`. This option is required. |
| `-p <port>`, `--port <port>` | Listen for network connections on `<port>`. The default is the build-time listener port from `src/config.h`. Ports must be non-negative. |
| `-s <dir>`, `--srcroot <dir>` | Use `<dir>` as the source root for saved source files. If omitted, `srcroot` in the current directory is used and created when missing. If supplied, the directory must already exist and be writable. |
| `--strict-validation` | Verify bytecode before runtime execution and while loading itemstores. |
| `--strict-runtime-contracts` | Report runtime argument contract mismatches that normal live-update operation intentionally tolerates, such as discarded item-call arguments while callers and callees are being updated. Stack effects and return values stay the same, but the extra checks add runtime overhead. |
| `-q`, `--quiet` | Suppress progress/status messages. |
| `-v`, `--verbose` | Print verbose progress and diagnostic trace messages. |

`sin` requires an object file even when an itemstore already exists: the object
is executed as the bootstrap code before the optional network loop starts.
At the default log level, `sin` reports lifecycle events such as itemstore
loading, source-root selection, listener startup, connection changes, and
shutdown. VM execution traces, boot return values, task return values, and
runtime repair/coercion details are shown only with `--verbose`.

When `sin` shuts down safely, it saves the itemstore using the selected
durability mode. `sys.abort` marks shutdown as unsafe, causing the runtime to
exit without the normal itemstore save.
