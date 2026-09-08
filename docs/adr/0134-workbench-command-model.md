# ADR 0134: Workbench Host, Selection Context and the Command Registry

Date: 2026-09-08 (Professional Workbench 5.0)
Status: Accepted (implemented in `src/app/workbench/`)

## Context

The 4.0 shell fixed the information architecture (ribbon-first, hidden
action-host menubar, TaskCenter thin-client law, Data/Results/Layers
separation) but three things remained ad hoc:

1. **Workspace lifecycle.** Classification, georeference (I2I/I2M), OBIA
   and the layout studio are lazy top-level windows with bespoke open
   slots. The shell cannot answer "which workspace is the user in, is it
   dirty, how do I switch safely".
2. **Selection semantics.** Every surface re-derives context itself
   (`currentLayer` probes in the ribbon, per-dialog combo fills, scattered
   `setEnabled` calls — 66 sites in `main_window_*.cpp` alone).
3. **Command identity.** One handler per capability existed, but its
   availability contract, shortcut ownership and reason strings lived
   nowhere; the layer context menu, ribbon and menus each re-encoded
   enablement.

## Decision

Three additive authorities in `src/app/workbench/`:

- **`IWorkbench` / `WorkbenchHost`** — a registration + switcher contract.
  Interactive exceptions stay exceptions: session windows register as
  *external* benches (`WorkbenchFeature::ExternalWindow`) wrapping their
  existing lazy-open slots. No MDI, no tab consolidation, no compute moves.
- **`SelectionContext`** — one debounced (≤150 ms) projection aggregated
  from authoritative sources (QGIS canvas/tree, Data Manager, governance
  browser, active bench). Pure `ContextRules` functions map snapshots to
  capability groups and human-readable unavailability reasons. Panels
  *push* their selections via notify slots; the context never includes
  panel types (keeps tests link-light).
- **`CommandRegistry`** — one `CommandDefinition` per capability: single
  handler (forwards to existing window slots), one availability predicate,
  one canonical shortcut (duplicate id/shortcut registration rejected).
  Ribbon buttons, the hidden action-host and the layer-tree context menu
  *project* the same definition; the palette triggers through
  `CommandRegistry::trigger` and can therefore never diverge.

## Consequences

- Enablement scattered across 66 `setEnabled` sites can migrate to
  definitions incrementally; migrated surfaces are contract-tested
  (`test_command_registry`, `test_selection_context`).
- The layer menu is uniformly Chinese and shares ribbon availability.
- Shortcut uniqueness is enforced at registration time — stronger than the
  menu-source scan, which remains as a tripwire for non-registry actions.
- `InspectorHost` (F) and `InteractiveSession` (H) reuse the same context
  and lifecycle types; no new state owners.
- Non-goals honored: no panel cross-access, no rebuild of TaskCenter /
  JobEngine / sessions, no processing loops in UI.

## References

- `docs/ui-architecture.md` Part II
- `tests/test_workbench_host.cpp`, `tests/test_selection_context.cpp`,
  `tests/test_command_registry.cpp`, `tests/test_command_palette.cpp`,
  `tests/test_inspector_host.cpp`
