# 01 — Canonicalize PluginHost Domain Glossary and ADR 0024

**What to build:**
Update the project domain model to define `PluginHost` as the single canonical concept and class name for plugin lifecycle management across desktop GUI, CLI runner, and test surfaces. Record the architectural decision in ADR 0024.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [x] `CONTEXT.md` updated to replace `PluginManager` with `PluginHost` in the domain glossary.
- [x] `docs/adr/0024-unify-plugin-host-facade.md` created, documenting the collapse of shallow double indirection into `PluginHost`.
