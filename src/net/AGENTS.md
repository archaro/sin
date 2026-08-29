# Networking guidance

Before modifying this directory, read
[`docs/internals/network.md`](../../docs/internals/network.md) and follow its
ownership, lifetime, and subsystem-boundary contracts.

`libtelnet.c` and `libtelnet.h` are integrated third-party sources. Preserve
their local style and avoid unrelated cleanup or Sinistra-specific behaviour.
If cleanup of these files is mandated, commit the changes separately to other
changes if possible.

Follow the repository-root `AGENTS.md` for general change and validation rules.
