# PLAN — implementation decisions (8.0)

Smallest-extension decisions, recorded per the autonomy contract.

## D1. Remote identity token (pkg B, geospatial side)

New `src/geospatial/remote/remote_identity_token.{h,cpp}`:

```
struct RemoteIdentityTokenOptions { timeouts/retries/probeBytes like RemoteValidatorOptions; }
std::string remoteIdentityToken(const std::string& url, const RemoteIdentityTokenOptions& = {});
```

- FAIL-CLOSED: returns "" unless the probed identity carries a **strong ETag**
  (byte-level freshness proof). Weak ETag / Last-Modified-only / size-only → ""
  (uncacheable; mirrors the layer's truthfulness contract).
- Token = `"ri1:" + hex(SHA-256(basis))` where basis = canonical URL **with
  credential-shaped query values removed** (reuse `isCredentialQueryKey`) + "\n" +
  strong ETag + "\n" + size when known. Content change ⇒ new ETag ⇒ new token;
  re-signed URL (signature value dropped) with same content ⇒ SAME token (no false
  invalidation); different content behind same path ⇒ different ETag ⇒ different token.
- Digest: never downloads the asset; identity probe is bounded (≤1 MiB, default 1 KiB
  ranged GET). CPL `CPL_GCP_CHECKSUM`-style hashing is out of scope (would need full
  body) — refused in docs.
- Credential safety: token is a SHA-256 over a basis whose credential-shaped values
  were removed; no raw URL/credentials in token or reports.

## D2. Resolver wiring (pkg B, data side)

- `src/data/execution_identity_bridge.{h,cpp}` (new, in sicnu_data):
  `void installGeospatialInputIdentityResolver();` — installs an `InputIdentityResolver`
  that maps `QString path → QString token` via `remoteIdentityToken()`; empty token ⇒
  empty string (uncacheable). Links `Sicnu::Geospatial` PRIVATE (guard-compatible: no
  GUI/QtNetwork deps added).
- Collectors (`temporal_workspace.cpp`, both the generic-datasource loop and the
  scene loop): when `dataManager->findByPath` fails for a remote-classified path,
  consult `executionIdentityResolver()`; a non-empty token yields
  `TaggedDerivationInput{ lazyContentDigest = token, valueDomain = "remote_identity" }`
  (assetId/revision unset — the token IS the identity); empty ⇒ current fail behavior
  with a precise reason ("remote input has no provable identity …").
- Hosts call the installer at startup: app (GUI + MCP branches), CLI main, worker,
  pipeline runner. Install is idempotent (set-once semantics preserved; later installs
  replace — existing contract of `setExecutionIdentityResolver`).

## D3. STAC datetime normalization (pkg D)

- New `src/geospatial/util/time_normalization.{h,cpp}` (Qt-free): parses ISO-8601
  instants with `Z`, `+hh:mm`, `+hhmm`, `+hh` offsets and naive timestamps
  (treated as UTC per STAC spec recommendation — flagged `assumedUtc`), computes
  `toUtcString()` (canonical `YYYY-MM-DDTHH:MM:SS(.fff)Z`) and a comparable epoch
  nanoseconds (int64, bounded years 0–9999). Total function: `parseIso8601Instant`
  returns a struct with `ok`, `assumedUtc`, `hadOffset`.
- `StacItem` gains `datetimeUtc`/`startDatetimeUtc`/`endDatetimeUtc` +
  `datetimeNormalized` flag (additive; `parse()` fills them).
- `buildTemporalSeries`: orders by parsed instant when both parse (raw-string compare
  only as last resort); undated last (unchanged); duplicate effective instants get a
  deterministic tie-break (item id, then original order) and are reported:
  `StacSeriesEntry` gains nothing — instead `buildTemporalSeries` keeps signature and a
  new `buildTemporalSeriesDetailed` returns `{entries, duplicates}` where duplicates =
  items sharing a normalized instant with an earlier entry. Existing callers unchanged.

## D4. Range cache hazard fix + fault evidence (pkg C)

- Fix: Unchanged revalidation path calls `updateEntrySize` ONLY when
  `validator.identity().hasSize` (never fabricate size 0).
- Extend `tests/support/http_range_server.h` with a bounded **concurrent** mode
  (thread-per-connection, max 4) + `Reset` behavior (accept, send headers, RST) —
  loopback-only, test-only.
- New tests in `test_io_range_cache.cpp`: concurrent readers same range (dedup: byte
  accounting < N×object), overlapping/adjacent window reads, mid-block connection
  reset → fallback serves correct bytes, changed content under same URL with
  `RevalidateOnOpen` → generation bump, no blended read; COG built over
  `/vsirangecache/` URL with overview window reads + byte accounting.

## D5. GeoParquet round-trip (pkg F)

- `VectorWriter`: accept driver short name `Parquet` (staging path identical to
  single-file drivers). Writer-side capability gate: `GDALGetDriverByName("Parquet")`
  with real create support, else typed refusal (honest degradation).
- Round-trip test (driver-gated): points/multipolygons + string/int/real/date fields +
  CRS (EPSG:4326 and a projected one) + null + empty-geometry semantics → write →
  read back via VectorReader → compare WKT-normalized geometry, attributes, CRS,
  field types. Metadata: verify the `geo` key metadata exists on the file.
- `format_profiles`: upgrade GeoParquet notes to certified-round-trip **only** for the
  verified configurations; keep the Arrow-dependency honesty note.

## D6. Multidim EO cube (pkg E)

- `DimensionInfo` gains optional string axis values (`hasStringValues`,
  `stringValues`, bounded by the same cap) captured from the indexing variable when
  numeric capture fails (CF datetime strings).
- `MultidimView::resolveCoordinateIndexByString` (exact match first; nearest on parsed
  instant when the axis parses as ISO-8601) → returns the same `CoordinateSliceMatch`.
- End-to-end bounded EO workflow test (netCDF driver-gated): generated small cube
  (time×y×x, CF time axis + string datetime axis variant), select a time slice by
  coordinate AND by string value, bounded window read, missing-value accounting,
  dimension-order fidelity assertions, maxCells bound enforced.

## D7. Doctor 3.0 additive checks (pkg H)

- `cacheability`: remote + strong ETag + acceptsRanges ⇒ "cacheable" verdict;
  weak/none ⇒ advice (never auto-changed).
- `multidim`: driver multidim API availability + variable/dim counts posture.
- `modern_vector`: GeoParquet/FlatGeobuf profile posture (accessible vs certified)
  with driver-gated honesty.
- `reproducibility`: identity section cross-check — remote identity with no strong
  validator warns that executions over it cannot prove freshness (advice only).
- All findings: read-only, advice-only (auto_fixable stays false).

## D8. CLI surface (pkg I)

- `data identity <url> [--revalidate]` — probe/revalidate JSON (redacted display).
- `data cache status|clear` — range-cache config + telemetry / clear (process-local,
  documented as such).
- `data stac` gains `search <root> [--bbox ...] [--datetime ...] [--collections a,b]
  [--limit N] [--max N]` — bounded, timeout-bounded; redacted hrefs only.
- Help/descriptor drift tests extended where the help registry enumerates `data`
  subcommands (verify location; the CLI usage string itself is drift-tested).

## D9. Cloud credentials (pkg G)

- `docs/io/cloud-credentials.md`: provider-neutral boundary — credentials live in
  env/CPL config only; never in project/dataset/trace/help/diagnostic/cache keys/PR
  fixtures; signed-URL query redaction rules (existing `isCredentialQueryKey`).
- Tests: `/vsis3/`-style + signed-query display redaction; identity/telemetry JSON
  leak sweep (loopback fixture with credential-shaped query in URL).

## Refusals (recorded)

- No read-ahead/prefetch policy change in the range cache without evidence (7.0
  clamps fetches to the read window; changing that alters byte accounting semantics
  contract-tested today).
- No full-asset content digest in identity (would violate "never download entire
  large assets merely to compute identity").
- No STAC write/transactions (out of scope by 7.0 contract).
- No new credential store / provider SDK (ADR 0139: CPL owns credentials).
