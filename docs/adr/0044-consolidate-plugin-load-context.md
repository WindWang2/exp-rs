# ADR 0044: Consolidate Plugin Load Context

## Status
Accepted

## Context
`PluginHost::loadPythonPlugin` manually unpacked three raw pointers
(`DataManager*`, `QMenu*`, `ActiveViewHost*`) from `SicnuAppInterface` and
threaded them individually through `PythonPluginHost::loadPlugin` and
`PythonPluginAdapter`'s constructor. This was a shallow "unpack and repack"
seam that obscured the contract and made the signatures fragile when new
context fields were added.

## Decision
1. **Introduce `PluginLoadContext` struct** in
   `src/python/isolated/plugin_load_context.h` consolidating the three raw
   pointers into a single aggregate.

2. **Primary constructors/methods accept `PluginLoadContext`**: Both
   `PythonPluginHost::loadPlugin` and `PythonPluginAdapter` now accept
   `const PluginLoadContext&` as their primary interface.

3. **Backward-compatible overloads retained**: Inline delegating overloads
   (in the headers) accept the old 3-pointer signature and forward to the
   `PluginLoadContext` variant, keeping existing callers compiling.

## Consequences
- **Single-object parameter threading** replaces 3 raw pointers.
- **Extensibility**: future context fields (e.g., `McpServer*`) are added to
  the struct with zero signature churn across the call chain.
- **Zero breaking changes**: backward-compatible overloads ensure existing
  callers compile without modification.
