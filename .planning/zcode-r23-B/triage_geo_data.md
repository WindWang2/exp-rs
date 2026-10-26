# Issue Verification Triage — Track r23-B (geospatial / data)

Worktree: `/home/kevin/projects/rs-studio/exp-rs-r23-B-20261026T164130Z`
HEAD: `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`
Issues verified from `.planning/zcode-r23-B/open_issues_raw.json` (baseline cited in issue bodies: `2d2881631f`).
Context claim checked: track r21-B (PR #1673) claimed fixes for #1545 #1548 #1587 #1637 — **all four claims verified true in code** (commits `987161c255`, `8e04302629`, plus `40674def9d` / `e39a0582c1` / `17b24d3942` / `493ab3e5fc` for the #1637 sub-items and twin bounds).

## Summary table

| Issue | Area | Verdict | Fix commit(s) | One-line evidence |
|---|---|---|---|---|
| #1545 | src/geospatial/remote/range_cache.cpp | **FIXED_OPEN** | `987161c255` (2026-10-19) | `validatorOptions()` now propagates `options.blockPrivateNetworks = config.blockPrivateNetworks;` (range_cache.cpp:292); all 3 probe call sites additionally catch `PermissionDenied` |
| #1548 | src/data/execution_identity_bridge.cpp | **FIXED_OPEN** | `8e04302629` (2026-10-19) | Cache lookup moved into its own scope; `probeAndStore` called only after the lock_guard is destroyed (execution_identity_bridge.cpp:158-165) |
| #1579 | src/geospatial/util/resource_uri.cpp | **FIXED_OPEN** | `987161c255` (2026-10-19) | `percentDecode( segment, decoded );` return value no longer gates refusal (resource_uri.cpp:973); only embedded NUL and literal `..` refuse |
| #1587 | src/data/governance/workspace_service.cpp | **FIXED_OPEN** | `8e04302629` (2026-10-19) | All four store writes checked; `mirrored` counts only committed rows; failure channel `workspace.mirror_failed` audit added (workspace_service.cpp:222-246, 325-346) |
| #1637-1 | src/geospatial/util/resource_uri.cpp | **FIXED_OPEN** | `987161c255` | `case ResourceKind::VirtualDataset: return "virtual_dataset";` added (resource_uri.cpp:362) |
| #1637-2 | src/geospatial/formats/format_profiles.cpp | **FIXED_OPEN** | `40674def9d` (2026-10-21) | `profileForPath` rewritten to longest-declared-extension dot-anchored suffix match — "zarr.json" now beats "json" (format_profiles.cpp:586-610) |
| #1637-3 | src/geospatial/products/product_registry.cpp (+ twins) | **FIXED_OPEN** | `987161c255`, `17b24d3942`, `493ab3e5fc` (2026-10-21) | `readFileInto` deleted (grep: zero hits in src/); all three `readFileText` copies chunk-bounded at 4 MiB with typed refusal |
| #1637-4 | src/geospatial/remote/probe_remote.cpp | **PARTIAL** | `e39a0582c1` (2026-10-17) | First-hop private-host gate added under the process policy (probe_remote.cpp:100-107), but `probeRemote` still rides the raw redirect-following `CPLHTTPFetch` with no per-hop guard — the redirect half of the issue remains (latent: no production callers) |
| #1593 | src/lab_pack/lab_pack.cpp | **FIXED_OPEN** (and **OUT_OF_SCOPE** for track B) | `8dab23436a` (2026-10-19) | `builder["stackLimit"] = 128;` + try/catch around `reader->parse` (lab_pack.cpp:249, 252-262); file is in `src/lab_pack/**`, outside B-owned modules |

Side check requested for #1593 ("src/cli data catalog search — is the numeric-flag parsing bug in src/cli or a B-owned module?"): the parsing lives entirely in **src/cli/cli_commands.cpp:2392-2493** (`data catalog search <root> [flags]`) — **not** a B-owned module — and it is **fixed**: every numeric flag parses through the ok-flag gate (`toDouble(&parsed)` at :2443, `toInt(&parsed)` at :2461 and :2480) with typed refusals, commit `36eaa49956` (2026-10-19, "fix(src/cli): resolve full-review findings #1595 #1596 #1597 #1633"). Nothing for track B to do there.

---

## Per-issue evidence

### #1545 [P1] range cache probe does not propagate the SSRF flag — FIXED_OPEN

Buggy claim: `validatorOptions()` copied only timeout/connect/retries; probes at :1775/:1879/:1953 ran on the unguarded CPL transport.

Fix commit (verified via `git log -1 -S`): `987161c255 2026-10-19 fix(src/geospatial): resolve full-review findings #1545 #1579 #1637`.

Current code — `src/geospatial/remote/range_cache.cpp:283-293`:

```cpp
RemoteValidatorOptions validatorOptions( const RangeCacheConfig &config )
{
  RemoteValidatorOptions options;
  options.timeoutSeconds = config.timeoutSeconds;
  options.connectTimeoutSeconds = config.connectTimeoutSeconds;
  options.maxRetries = config.maxRetries;
  // R15-B R4-01: the probe / revalidation / Stat requests of a guarded
  // cache are guarded too (host check + redirect guard + DNS pinning) — the
  // policy is the cache's, not only its ranged fetch's.
  options.blockPrivateNetworks = config.blockPrivateNetworks;
  return options;
}
```

The commit also wraps all three consumer call sites — `revalidate` (:1892), `probeNewEntry` (:2009), `RemoteSourceValidator::probe` (:2103) — in try/catch for `ErrorCode::PermissionDenied` so a policy refusal becomes an invalidation / CPL error instead of escaping into GDAL VSI C callbacks (e.g. range_cache.cpp:2004-2017: `CPLError( CE_Failure, CPLE_AppDefined, "vsirangecache: %s", error.what() ); return nullptr;`). Because `RemoteValidatorOptions.blockPrivateNetworks = true` arms `remote_source_validator.cpp`'s redirectGuard (`fetchOptions.redirectGuard = ...` at remote_source_validator.cpp:376 and :471, running on the libcurl transport with `CURLOPT_FOLLOWLOCATION, 0L` per http_fetch.cpp:681), the "probe must ride the controlled transport" half of the fix direction is satisfied. Bonus hardening in the same file at :874-875: the process-wide policy (`offline::blockPrivateNetworks()`) forces `effective.blockPrivateNetworks = true` for the cache. Tests exercising `blockPrivateNetworks` exist in `tests/test_io_range_cache.cpp` and `tests/test_io_remote_validator.cpp`.

### #1548 [P1] identity bridge cold path self-deadlock — FIXED_OPEN

Buggy claim: `return probeAndStore( canonicalPath );` evaluated while holding `sessionCache().mutex`, and `probeAndStore` re-locks the same non-recursive mutex → guaranteed self-deadlock on cache miss. Resolver installed process-wide at startup and called on the submission path.

Fix commit (verified via `git log -1 -S'known = sessionCache().find'`): `8e04302629 2026-10-19 fix(src/data): resolve full-review findings #1548 #1587`.

Current code — `src/data/execution_identity_bridge.cpp:156-165`:

```cpp
  SessionEntry entry;
  bool known = false;
  {
    std::lock_guard<std::mutex> lock( sessionCache().mutex );
    known = sessionCache().find( canonicalPath, &entry );
  }
  if ( !known )
    return probeAndStore( canonicalPath ); // first sight: probe unlocked
```

The diff of `8e04302629` shows exactly the old in-lock `if ( !sessionCache().find(...) ) return probeAndStore(...);` being split into scoped-lock lookup + unlocked probe — matching the issue's suggested fix direction and the `remote_identity_resolver.cpp` reference pattern. Production path confirmed still routed through the fixed function: `installGeospatialInputIdentityResolver()` defined at execution_identity_bridge.cpp:176, installed at `src/app/main.cpp:163` and `src/cli/main_cli.cpp:126`.

### #1579 [P2] resolveAgainst rejects literal '%' filenames — FIXED_OPEN

Buggy claim: `const bool isDecoded = percentDecode( segment, decoded ); if ( !isDecoded || ... )` turned any malformed/literal '%' into `escaped = true` → Invalid + misleading "'..' traversal" reason; `cloud_50%.tif`-style assets refused.

Fix commit: `987161c255` (same sweep as #1545).

Current code — `src/geospatial/util/resource_uri.cpp:970-983`:

```cpp
    std::string decoded;
    // R15-B (#1505 T9): a literal / malformed '%' ("100%_coverage.tif",
    // "file%Q1.tif") is NOT a traversal — percentDecode keeps it verbatim
    // and reports !clean. Refuse only what decodes to something unsafe
    // (an embedded NUL here, '..' below); before this every such filename
    // was rejected as a traversal attempt.
    percentDecode( segment, decoded );
    if ( decoded.find( '\0' ) != std::string::npos )
    {
      escaped = true;
      break;
    }
    if ( segment == ".." || decoded == ".." )
```

The `!isDecoded` clause is gone; refusal now depends only on embedded NUL and the explicit `..` tests (which cover `%2e%2e` per tests/test_io_uri.cpp:241-249, unchanged and still passing semantics). Minimal and complete.

### #1587 [P2] mirrorAllAssets discards every governance write result — FIXED_OPEN

Buggy claim: the flush lambda did `( void ) m_store.upsertAssets( batch );` etc. for all four writes and returned traversed-row count as `mirrored`, which `data_project_serializer.cpp` published as "assets indexed" + audit `mirroredAssets` even when the store rejected every write.

Fix commit: `8e04302629 2026-10-19 fix(src/data): resolve full-review findings #1548 #1587`.

Current code — `src/data/governance/workspace_service.cpp`:

- :222-226 — `const Result<void> written = m_store.upsertAssets( batch ); ... if ( written ) mirrored += staged; else ++failedAssetBatches;` (with the header comment at :202-206: "`mirrored` counts rows the store actually COMMITTED ... (issue #1587)").
- :231 `if ( !m_store.addLineageEdges( edgeBatch, /*replaceOutgoing=*/true ) ) ++failedEdgeBatches;`
- :237 `if ( !m_store.upsertRun( it.value() ) ) ++failedRuns;`
- :243 `if ( !m_store.addRunOutputs( runOutputBatch ) ) ++failedRunOutputBatches;`
- :325-346 — failure channel: `qWarning( "WorkspaceService: mirrorAllAssets store writes failed ..." )` plus a durable audit record `workspace.mirror_failed` carrying `failedAssetBatches/failedEdgeBatches/failedRuns/failedRunOutputBatches/mirrored/prepared`.

Consumer `src/app/data_project_serializer.cpp:724-732` now receives a truthful count and a separate failure audit. Residuals observed, both outside the issue's stated scope (mirrorAllAssets' four writes): `( void ) m_store.removeAsset(...)` at :318 in the opt-in ghost-reconciliation branch (deliberate cascade of an already-dead row, commented) and `( void ) m_store.addLineageEdges(...)` at :495 in `mirrorAsset` (single-asset path). Neither regresses the reported defect.

### #1637 [P3] src/geospatial tech debt, 4 sub-items — 3 FIXED_OPEN, 1 PARTIAL

#### Sub-1: resourceKindName() missing ResourceKind::VirtualDataset — FIXED_OPEN

Fix commit: `987161c255` added the case. Current code — `src/geospatial/util/resource_uri.cpp:352-370`:

```cpp
const char *resourceKindName( ResourceKind kind )
{
  switch ( kind )
  {
    case ResourceKind::LocalFile: return "local_file";
    ...
    case ResourceKind::VsiVirtual: return "vsi_virtual";
    case ResourceKind::VirtualDataset: return "virtual_dataset";   // <- line 362
    ...
```

Consumers (doctor/data_doctor.cpp, probe/probe.cpp, identity/asset_identity.cpp) now see `"virtual_dataset"` instead of `"invalid"` for `vrt://`/`virtual://` resources. Minor residue: tests/test_io_uri.cpp:263-267 still pins only the original 5 names — no test asserts `"virtual_dataset"`.

#### Sub-2: profileForPath last-dot + first-match makes "zarr.json"/STAC "json" unreachable — FIXED_OPEN

Fix commit: `40674def9d 2026-10-21 fix(geospatial): resolve two-segment extensions in profileForPath (#1637-2)` (found via `git log -1 -S'#1637-2'`). Current code — `src/geospatial/formats/format_profiles.cpp:586-610`:

```cpp
const FormatProfile *FormatRegistry::profileForPath( const std::string &path ) const
{
  ...
  // Longest DECLARED extension wins, dot-anchored at its start; equal-length
  // ties keep the first-registered profile (GeoJSON vs STAC on "json",
  // GeoTIFF vs COG on "tif"). The previous last-dot single-segment match
  // made multi-segment declarations unreachable (#1637-2): ...
  const std::string lowered = lowerAscii( cleanPath );
  const FormatProfile *best = nullptr;
  std::size_t bestLen = 0;
  for ( const FormatProfile &profile : mProfiles )
    for ( const std::string &extension : profile.extensions )
    {
      if ( extension.size() <= bestLen || extension.size() + 1 > lowered.size() ) continue;
      const std::size_t at = lowered.size() - extension.size();
      if ( lowered[at - 1] != '.' || lowered.compare( at, extension.size(), extension ) != 0 ) continue;
      best = &profile; bestLen = extension.size();
    }
  return best;
}
```

`"store.zarr.json"` now suffix-matches the 9-char `zarr.json` declaration over the 4-char `json`. Tests added in tests/test_io_paths.cpp:446-483 (`profileForPath("/data/store.zarr.json")`, a `/vsicurl/...?token=` URL, and bare `zarr.json`).

#### Sub-3: readFileInto dead unbounded reader (+ unbounded twins) — FIXED_OPEN

- Dead code removed: `987161c255` deleted `readFileInto` from product_registry.cpp (diff removes the 12-line function plus the now-unused `<fstream>`/`<sstream>` includes). `grep -rn "readFileInto" src/` → zero hits at HEAD.
- Twin bounded: `17b24d3942 2026-10-21 fix(geospatial): bound CN metadata sidecar reads at 4 MiB (#1637-3 twin)`. Current `src/geospatial/products/cn_product_metadata.cpp:117-142`: opens via `sicnu::portable::pathFromUtf8`, chunk-reads 64 KiB at a time and throws `GeoError(ErrorCode::InvalidMetadata, "product metadata exceeds the 4 MiB bound, refused while reading: ...")` once total exceeds `kMaxMetadataXmlBytes = 4 MiB`.
- Sibling copies bounded in the same sweep: `493ab3e5fc 2026-10-21` — product_adapters.cpp:66-93 (`kMaxSidecarBytes`, chunked refuse) and sensor_profile.cpp:63-91 (`kMaxProfileBytes`, chunked refuse, wide path per #1178).

Residue note (different defect class, not this sub-item): product_adapters.cpp:74 still opens with a narrow `std::ifstream in( path, ... )` (no `pathFromUtf8`), unlike its two siblings — a Windows non-ANSI-path gap (#1178 class), while the unbounded-read defect named here is gone from all copies.

#### Sub-4: probeRemote has no egress/SSRF policy — PARTIAL

Fixed half — commit `e39a0582c1 2026-10-17 fix(remote,cli): R4-03 process-wide private-network policy for non-range-cache remote paths`. Current code — `src/geospatial/remote/probe_remote.cpp:99-107`:

```cpp
  // R15-B (R4-03): CPLHTTPFetch has no per-hop guard; under the process
  // policy a private origin is refused before dispatch (the GDAL-level
  // fetch hook repeats the check for every CPLHTTPFetch).
  if ( offline::blockPrivateNetworks() && offline::isPrivateHost( target ) )
  {
    Json::Value details;
    details["url"] = uri.display();
    throw GeoError( ErrorCode::PermissionDenied,
                    "probeRemote: private / loopback / link-local hosts are blocked", details );
  }
```

Plus the process-level backstop installed by `engageProcessPrivateNetworkBlocking()` — `process_network_policy.cpp:185-216` installs, once, VSI handler wrappers and `CPLHTTPPushFetchCallback( policyFetch, nullptr )` where `policyFetch` (:161-176) refuses any private URL at CPLHTTPFetch entry. This closes the issue's primary complaint ("no egress/SSRF policy at all") for first hops.

Remaining gap (why PARTIAL):
1. `probeRemote` still dispatches through raw `CPLHTTPFetch`, and this GDAL build hardcodes redirect following — stated in-tree at `src/geospatial/remote/http_fetch.cpp:26-31`: "CPLHTTPFetch follows redirects UNCONDITIONALLY in this GDAL (3.12.4 hardcodes CURLOPT_FOLLOWLOCATION=1 ...) A first-hop egress check made before httpFetch therefore does NOT cover redirect targets". The `policyFetch` callback sees only the initially requested URL (redirects resolve inside one libcurl transfer), so a public first hop 302-ing to `169.254.169.254`/RFC 1918 is still fetched. The redirect-aware transport exists (`HttpFetchOptions::redirectGuard`, http_fetch.cpp:344-356 and :681 `CURLOPT_FOLLOWLOCATION, 0L` + DNS pinning) but probeRemote does not use it.
2. `RemoteProbeOptions` (probe_remote.h:27-36) still has no `blockPrivateNetworks` field; the gate keys only off the process-wide `offline::blockPrivateNetworks()` — fine as design, but per the issue text the option surface is unchanged.

Affected production path / latency: `grep -rn "probeRemote" src tests` shows **no production caller** — only tests (test_io_remote_range.cpp, test_offline_mode.cpp) and comments (process_network_policy.h:12, remote_source_validator.h:134; the `probeRemote` bool at src/data/governance/workspace_validator.h:25 is an unrelated field). The defect is latent, exactly as the issue noted.

Minimal correct fix direction: build probeRemote's request on the guarded transport — `httpFetch` with `HttpFetchOptions::redirectGuard` that runs `offline::isPrivateHost` (+ `resolveForPinning`) on every Location hop, replacing the raw `CPLHTTPFetch` call at probe_remote.cpp:120; optionally add a per-call `RemoteProbeOptions::blockPrivateNetworks` defaulting to the process policy.

### #1593 [P2] lab_pack JSON parse without stackLimit/try-catch — FIXED_OPEN (code), and OUT_OF_SCOPE for track B

Fix commit: `8dab23436a 2026-10-19 fix(src/lab_pack): resolve full-review findings #1593` (found via `git log -1 -S'stackLimit' -- src/lab_pack/lab_pack.cpp`). Current code — `src/lab_pack/lab_pack.cpp:242-262`:

```cpp
  Json::Value root;
  Json::CharReaderBuilder builder;
  // Pinned, not inherited: the jsoncpp default stackLimit is version-dependent
  // (1000 on 1.9.5/1.9.6), and the reader THROWS (not returns false) once the
  // limit is exceeded. ...
  builder[ "stackLimit" ] = 128;
  std::string parseErrors;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  try
  {
    if ( !reader->parse( bytes.data(), bytes.data() + bytes.size(), &root, &parseErrors ) )
      return PackLoadResult::failure( "lab.pack_schema", contextError( errorContext, parseErrors ) );
  }
  catch ( const Json::Exception &exception )
  {
    return PackLoadResult::failure(
        "lab_pack_schema",
        contextError( errorContext, std::string( "invalid JSON: " ) + exception.what() ) );
  }
```

Both halves of the issue (explicit stackLimit + exception guard) are in place, matching the `plugin_manifest.cpp`/`ipc_envelope.cpp` discipline the issue cited. Scope note: the primary file is `src/lab_pack/lab_pack.cpp` (escape chain through `src/agent`, `src/cli/lab_self_check.cpp`, `src/teaching_admin`) — **outside track B's owned modules** (`src/geospatial/**`, `src/data/**`, `src/dataset/**`, `src/experiment/**`, `src/operators/runtime/**`, `src/runtime/**`, `models/**`), so nothing here is B-owned regardless; the fix landed via the lab_pack track.

Side check (per task note): the "data catalog search" numeric-flag parsing is implemented wholly in `src/cli/cli_commands.cpp:2385-2493` — grammar `data catalog search <root> [flags]`, flag loop at :2400-2493 with `--cloud-max` via `toDouble(&parsed)` (:2443), `--limit` via `toInt(&parsed)` (:2461), `--max-items` via `toInt(&parsed)` (:2480), all with typed `CliErrorDetails` refusals. It is **not** in a B-owned module (src/cli is outside B scope) and is already fixed by `36eaa49956 2026-10-19 fix(src/cli): resolve full-review findings #1595 #1596 #1597 #1633`.

---

## Verdict legend

- **LIVE** — bug present as described. **FIXED_OPEN** — verified fix in code (commit cited). **PARTIAL** — fix exists, remaining gap described. **OUT_OF_SCOPE** — file outside B-owned modules. **UNVERIFIABLE** — could not confirm either way. (No issue in this batch is LIVE or UNVERIFIABLE.)

## Bottom line for track B

- r21-B's PR #1673 claim is accurate for #1545, #1548, #1587, #1637(-1/-2/-3); the only live (though latent, zero production callers) residue in B-owned code is **#1637 sub-4's redirect half**: probeRemote should move onto the guarded `redirectGuard` transport.
- #1593 is fixed by another track and lives outside B ownership; the src/cli catalog-search numeric-flag parsing is also fixed and also outside B ownership.
