# ADR 0042: Deepen AuthResolver Credential Cache Seams

## Status
Accepted

## Context
`AuthResolver` was a single-method interface (`applyAuthConfig`) that applied an
auth-config id to a URI. Callers needing to query whether a credential existed,
or to enumerate known configs for UI population, had to reach directly into
`QgsAuthManager` — bypassing the injectable resolver seam and breaking headless
testability.

## Decision
1. **`hasAuthConfig(authConfigId)` seam**: Expose a pure-virtual query for
   whether a given auth config id is registered with the resolver. Returns true
   for empty ids (open services). The production `QgisAuthResolver` delegates to
   `QgsApplication::authManager()->configIds().contains(id)`.

2. **`knownAuthConfigIds()` seam**: Expose a pure-virtual enumeration of all
   known auth config ids. The production implementation delegates to
   `QgsAuthManager::configIds()`. This never returns credential material — only
   opaque string ids.

3. **Credential discipline preserved**: Neither new method exposes passwords,
   tokens, or secrets. The contract remains: credential material flows only
   through `QgsAuthManager`'s internal provider path, never through the resolver
   interface.

4. **Test stub updated**: `RecordingAuthResolver` in
   `test_remote_map_display.cpp` implements both methods with trivial logic
   (`hasAuthConfig` returns `acceptConfig`, `knownAuthConfigIds` returns empty).

## Consequences
- **Callers no longer need `QgsAuthManager` directly** to check config existence
  or enumerate available configs — they go through the injectable `AuthResolver`.
- **Headless / test environments** can now stub both query and enumeration
  without `QgsApplication` initialization.
- **Credential discipline maintained**: the new methods expose only opaque ids.
