# Declarative Out-of-Process UI (plugin-platform 8.0)

Out-of-process plugins describe UI as bounded JSON; the ExpRS host renders
it natively and owns every widget. No plugin pointer, no plugin thread and
no plugin byte ever enters the host's GUI process state — a plugin crash
cannot take the UI down and a UI unload cannot race plugin code.

## The two halves

| half | owner | contract |
|---|---|---|
| describe | plugin (worker) | `EXPRS_createUiSchemaProviderV1` returns a schema; the worker validates it fail-closed and answers `ui.describe` |
| render + invoke | host (GUI process) | `PluginUiSchemaRenderer` (framework) maps the schema to host-owned widgets, attaches them through the reverse-ownership `UiShellSink`, and delivers bounded events via `ui.invoke` |

Absence of the entry point is the normal "no declarative UI" answer
(E6008) — a plugin binary without UI keeps loading cleanly.

## Schema v1 (validated, hard-capped)

Surfaces: `commands`, `menuItems`, `settingsPages`, `dockPanels`,
`contextActions`, `helpTopics`. Controls: `label`, `text`, `number`,
`checkbox`, `combo`, `slider`, `button`, `group` (nested, depth ≤ 4).

Caps (see `exprs::PluginUiSchemaLimits`): ≤ 32 entries, ≤ 64 controls per
page, ≤ 32 combo options, ≤ 256-char strings. Unknown control TYPES fail
validation (the host cannot render what it does not know); unknown FIELDS
are ignored (additive evolution). `menuItems` and `contextActions` must
reference declared `commands`.

## Event flow

```
host widget interaction
  -> {contributionId, controlId, eventType: changed|clicked|command, value?}
  -> bounded queue (64; drop-oldest + counter) on one serialized thread
  -> ui.invoke (quota-gated, bounded deadline)
  -> {response: {state?: {controlId: value}}} applied to host widgets
     (queued onto the GUI thread; a released plugin's responses are dropped)
```

Events count against the plugin's request concurrency; a wedged plugin
delays one event, never the UI thread.

## Lifecycle

- attach: after `plugin.load`, the host fetches the schema and renders it
  (`PluginRuntimeHost::describeUiSchema` + `PluginUiSchemaRenderer::
  attachPluginSchema`).
- reload: re-attachment replaces the previous rendering.
- unload: `PluginRuntimeHost` detach happens in the same lifecycle slot as
  the in-process UI release (before the worker dies); in-flight responses
  for a detached plugin are dropped by generation-free shared ownership.

## Authority boundaries (unchanged)

Commands and menu items render as host actions; the shell decides
placement, shortcuts and `CommandRegistry` registration. Selection and
context authority stay with `SelectionContext` / the workbench seams —
`contextActions` are declared and delivered to the host, whose shell
surfaces decide placement (shell integration is the workbench track's
seam; the protocol contract above is stable).
