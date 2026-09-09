# Workbench lifecycle — target contract

```
Workbench (IWorkbench)
 ├─ activate()/deactivate()          — enter/leave; lazy construction allowed
 ├─ augmentSelectionContext()        — projection into the shared context
 ├─ commands                         — workbench.* CommandDefinitions
 ├─ inspector contributions          — InspectorSection registration
 ├─ interactive session              — InteractiveSession adapter (dirty/in-flight/cancel)
 ├─ dirty state                      — isDirty() consultable by shell close policy
 ├─ in-flight tasks                  — hasInFlightCompute() via TaskCenter seam
 ├─ cancel/close policy              — requestCancel()/requestClose() confirm dirty
 └─ persistence                      — saveState()/restoreState() under QSettings bucket
```

## As-is vs to-be (per bench)

| Bench | as-is lifecycle | to-be |
|-------|-----------------|-------|
| map | activate raises canvas stack; no dirty concept | unchanged (embedded, always "present") |
| classify | opener-only; adapter exists but unwired | windowGetter + dirtyFn(isSessionDirty) + inFlight(hasInFlightCompute) + cancel(cancelInFlightCompute) + closeFn delegated to window closeEvent |
| georef-i2i / i2m | opener-only | windowGetter + dirtyFn(rs_georeferencing_session::isDirty) + closeFn |
| obia | opener-only | windowGetter + closeFn (+ dirty when window exposes it) |
| layout | opener-only | windowGetter + closeFn (designer owns its own confirm) |

## Shell close policy (target)

Quit → for each active/instantiated bench: if hasInFlightCompute() → surface
cancelling choice; if isDirty() → requestClose() (bench confirms its own dirty
state). WorkbenchHost::activate() remains synchronous and must not silently
destroy a dirty bench's state.
