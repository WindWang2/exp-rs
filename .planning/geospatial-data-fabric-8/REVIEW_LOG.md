# REVIEW LOG — adversarial review + remediation

Two read-only reviewer subagents (A: architecture/correctness/concurrency;
B: tests/portability/security/docs-claims) reviewed `226adb8d02..HEAD`.
Primary-agent self-review findings included. Dispositions below; every P0/P1
fixed, every actionable P2 fixed, P3s fixed unless noted.

## A (architecture/correctness)

| # | Sev | Finding | Disposition |
|---|-----|---------|-------------|
| A1 | P1 | Series comparator not a strict weak ordering (mixed instant/raw-string legs cycle; UB in stable_sort; demonstrated with standalone build) | **FIXED**: class-split comparator — parseable instants always sort before unparseable; raw-string order only within the unparseable class. Regression test added (broken datetime among offsets). |
| A2 | P1 | parseIso8601Instant int64 nanosecond overflow (UBSan: silent wrap for years ≈1678–2262 while header claimed 0000–9999) | **FIXED**: refuse \|seconds\| > 9223372035 (ok=false); header documents the representable range. Tests: 2263/1677/9999/0001 refused; 2262 accepted; negative-epoch round-trip; leap-second clamp. |
| A3 | P2 | Fixture destructor `close()` on fd owned by another thread (POSIX does not wake peer recv; fd-reuse hazard) | **FIXED**: in-flight socket SET; teardown `shutdown()`s (never closes); only the owning thread closes. |
| A4 | P2 | `mCurrentClient` single slot clobbered by concurrent handlers | **FIXED**: replaced by mutex-guarded `mInFlight` set (serial + concurrent modes). |
| A5 | P3 | `data identity` probes the origin twice | **FIXED**: `remoteIdentityTokenFromIdentity()` derives the token from the already-captured identity (single probe). |
| A6 | P3 | `data cache check` no RAII between install/uninstall | **FIXED**: scope guard uninstalls on every exit path. |
| A7 | P3 | Profile JSON advertises capabilities=true when driver missing | **FIXED**: capability booleans ANDed with driver availability. |
| A8 | P3 | GDAL<3.9 uninstall leaks one handler per cycle | **FIXED**: uninstall keeps the (underegistrable) handler and `installed()` stays true — honest state instead of leak; documented in code. |
| A9 | P3 | Token basis hygiene: control chars in ETag could forge field boundaries; percent-encoded keys evade the scan; host case forks identity | **FIXED**: ETags with control chars are refused (fail-closed); keys percent-decoded before the credential scan; host lowercased in the basis. |
| A10 | P3 | `datetimeNormalized` trivially true for range-only items; string-axis `resolvedValue` contradicts header contract | **FIXED**: flag is per-declared-field only (range-only derivation does not set it; regression test added); header documents the string-axis index exception. |
| A11 | P3 | `setConcurrency` unsynchronized vs serve loop; fingerprint probes add bounded network time to admission | **FIXED**: `mMaxConnections` is atomic. Probe budget: bridge uses shortened timeouts (5s/3s/0 retries) + comment; memoization noted as follow-up. |
| A12 | P3 | Stale "detached handlers" comment; token-shape doc `ri1:` vs `ri1:v1:` | **FIXED** (joined handlers comment; header shows `ri1:v1:<64 hex>`). |

## B (tests/portability/security/claims)

| # | Sev | Finding | Disposition |
|---|-----|---------|-------------|
| B1 | P1 | Bridge turns uncacheable into UNCAUGHT GeoError for non-remote spellings (`/vsimem/`, `/vsizip/`, `PG:`) on the admission path | **FIXED**: bridge wraps the probe in try/catch → "" (fail-closed); matches the header contract. |
| B2 | P2 | Comparator strict-weak-ordering violation (same root as A1, independent counterexample) | **FIXED** (see A1). |
| B3 | P2 | VSI spellings keep credentials in the token basis (canonical() is not redacted); re-signed `/vsicurl/` URLs fork identity | **FIXED**: VsiRemote payloads are re-parsed and routed through the same credential scan; regression test asserts `/vsicurl/` + signed query ⇒ same token as plain. |
| B4 | P2 | `data cache check` echoes credential-bearing URLs (payload + error message) | **FIXED**: both outputs now use `ResourceUri::display()`. |
| B5 | P2 | Blocking probes on the submission path, uncached | **PARTIALLY FIXED**: shortened probe budget in the bridge (5s/3s/0 retries) + comments; process-wide negative-result memoization recorded as follow-up (deliberate scope tradeoff; remote inputs are usually registered and never reach the fallback). |
| B6 | P3 | CLI redaction test vacuous (URL carried no secret) | **FIXED**: test now passes a signed URL with a secret and asserts its absence. |
| B7 | P3 | Hex-digest substring checks cannot fail | **FIXED**: replaced by direct `remoteIdentityBasis()` non-containment assertions. |
| B8 | P3 | Single-slot mCurrentClient in concurrent mode | **FIXED** (see A4). |
| B9 | P3 | Ranged HEAD could consume the ResetRanged fault | **FIXED**: arm condition requires `!isHead`; `resetFired()` accessor lets the reset test prove the fault fired. |
| B10 | P3 | Handler leak per cycle on GDAL<3.9 | **FIXED** (see A8). |
| B11 | P3 | Double probe in `data identity` | **FIXED** (see A5). |
| B12 | P3 | Concurrent-reader byte bound loose (would pass ~3.5× duplication) | **FIXED**: tightened to < 2× window block bytes (dedup observed ≈ 266 KB; no-dedup ≈ 1.05 MB fails clearly). |
| B13 | P3 | COG bound margin build-dependent | ACCEPTED as-is (measured ~40% vs 50% cutoff; floor assertion `fetched > 0` present); noted as potentially flaky on very different GDAL builds. |
| B14 | P3 | Reset test comment lies; proof indirect | **FIXED**: comment corrected; `resetFired()` asserted. |
| B15 | P3 | NC_STRING fixture hard-fails on netCDF-3-only builds | **FIXED**: WARN+skip on container-format failure. |
| B16 | P3 | `%lld` in Qt-free code on legacy MinGW | ACCEPTED: UCRT/MinGW-w64 correct; repo targets MSVC/vcpkg + Linux; no action. |
| B17 | P3 | `--bytes` invalid values silently default; leap-second/negative-epoch untested; handler vector grows per connection | **FIXED**: `--bytes` now errors on invalid input; leap-second + negative-epoch tests added; handler vector accepted (bounded by test request count, joined at teardown). |

## Primary-agent self-review

- Seam doc/impl mismatch: `setExecutionIdentityResolver` returns a pointer to
  the installed resolver, not "the previously installed" — header corrected;
  the temporal test saves/restores the global explicitly.
- 7.0 drift fix: `DimensionInfo::toJson` dropped numeric axis values
  (fromJson read them) — serialization made symmetric (covered by round-trip
  test).

## Verified correct by reviewers (no action)

Range-cache P0 lifetime fix (incl. no double-register path), updateEntrySize
guard, SHA-256 vs FIPS vectors, permutation-sort mechanics and duplicate
accounting, layering (sicnu_data → sicnu_geospatial one-way PRIVATE; hosts
install once; no geospatial→data references), StacItem toJson verbatim wire
forms, DimensionInfo JSON symmetry, doctor advice-only verdicts, fixture
teardown ordering, Winsock paths + ws2_32 wiring, credential-strip for
RemoteHttp spellings, driver-gated test honesty.

## Follow-up findings (final integration, primary agent)

| # | Sev | Finding | Disposition |
|---|-----|---------|-------------|
| F1 | P1 (pre-existing on master) | `runHelpProjections` used `QCommandLineParser::process()`, which EXITS the whole program on unknown options — every CLI 3.0 subcommand invoked with `--json` (e.g. `algorithms list --json`, `data identity --json`) died with "unknown option" before dispatch; `test_cli_commands_json` could not pass in this configuration | **FIXED**: `parse()` instead of `process()`; a failed parse means "not a help projection" and falls through; explicit `--help` handling preserved |
| F2 | P2 (own test) | temporal resolver test: fake resolver matched `contains("provable")` which also matches "unprovable.tif" — the negative case could never fire | **FIXED**: exact-path match in the fake |
| F3 | P2 (own code) | CLI remediation edit accidentally dropped the read/telemetry lines of `data cache check` (caught by the heavy build) | **FIXED** |

## Follow-up findings (final sweep, primary agent)

| # | Sev | Finding | Disposition |
|---|-----|---------|-------------|
| F4 | P1 (own code) | Fixture destructor joined handler threads UNDER `mHandlerMutex` while a finishing handler needs that mutex for its `mInFlight.erase` — guaranteed self-deadlock whenever a handler was in-flight at teardown (reproduced: `test_io_range_cache` hung 3/3 runs at the concurrency test) | **FIXED**: join outside the lock (move the vector out, join, then clear leftovers); the in-flight shutdown loop also copies the set and shuts down outside the lock |
| F5 | P2 (environmental) | Fixture `send()` on a peer-closed socket raised SIGPIPE and killed the test process (exit 141, observed once in a full-suite sweep) | **FIXED**: fixture ignores SIGPIPE (POSIX) — send() return values are already checked |
| F6 | P3 (own test) | CLI identity test asserted the literal key name "X-Goog-Signature" absent from output, but the redaction contract masks VALUES and keeps key names in display forms; tightened to the secret-value assertion only | **FIXED** |
| F7 | P3 (own code) | `data cache` grammar clashed with `commandData`'s positional-path convention (`cache check <url>` consumed "check" as the path) | **FIXED**: `data cache <url> [--bytes N]`; usage string and test updated |

Post-fix evidence: `test_io_range_cache` 3/3 clean runs (84 assertions, 13 cases);
all fixture-dependent suites re-verified green (validator 14, remote_range 5,
stac_client 16, identity 5, doctor 8, stac 4); `test_temporal_workspace` 20/20
(264 assertions); `test_cli_commands_json` 6/6 (33 assertions).
