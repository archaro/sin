# Sinistra Library Calls

Libraries look remarkably like items, but are always strictly two layers deep, and are read only.  Do not try to assign a value to a library function: no good will come of it.

The Sinistra-speak for calling a library function is 'libcall'.  All libcalls return a value, even if that value is simply `nil`.

For a list of all current libcalls, see the [Libcalls Reference](../reference/libcalls.md).
