# Plan — RS14-01 Scientific Data Passport (RemoteSensingAssetState)

## Problem statement

Students and agents currently learn what a remote-sensing asset *is* by spelunking three disconnected truth surfaces: catalog snapshots (`AssetSnapshot`), file-level GDAL `SICNU_*` metadata, and assorted sidecars. None of them answers "what is this image's scientific state right now, which facts are declared vs inferred vs assumed vs missing vs contradictory". The FSM's "missing marker ⇒ DigitalNumber" is a silent default; SAR's dual-key conflict is confined to `sar_metadata`; nothing diffs state before/after an operator runs.

## User stories

- **Undergraduate (teaching)**: After loading a Landsat/S2 scene, the student opens the passport view and reads: identity/sensor, acquisition time (and where it came from), band roles and wavelengths (declared vs inferred from sensor profile), radiometric state (declared vs assumed-by-default), geometry/CRS, NoData/quality/cloud info, and explicit lists of *missing*, *inferred*, *assumed*, *conflicted* items — before running any analysis.
- **AI Agent (machine)**: Given an asset id or path, the agent queries a versioned, deterministic JSON passport (`sicnu.asset_state.v1`) through a read-only tool / CLI, uses `claim.kind` to reason about trust, and after a transformation calls `state diff` to see exactly what changed. It never plans repairs from this surface (other tracks do that) and never receives silently-corrected facts.

## Architecture

```
facts (plain DTOs, source-tagged)            pure projection              surfaces
┌──────────────────────────┐   ┌───────────────────────────────┐   ┌──────────────────┐
│ DatasetFacts   (GDAL)    │   │ AssetStateResolver            │   │ CLI `passport`   │
│ CatalogFacts   (Qt data) │ ─▶│  · vocabulary normalization   │ ─▶│ Agent tool (RO)  │
│ SensorProfileFacts       │   │  · claim lattice              │   │ Teaching view    │
│ DerivationFacts (Qt)     │   │  · conflict detection         │   │ diff API         │
│ SidecarFacts   (JSON)    │   │  ▼ RemoteSensingAssetState    │   │ library API      │
└──────────────────────────┘   └───────────────────────────────┘   └──────────────────┘
```

- **Layer 0 — core** (`src/scientific_state/`, lib `sicnu_scientific_state`, namespace `sicnu::state`, **jsoncpp only, no Qt/GDAL**): value types, claim lattice, resolver, JSON (`sicnu.asset_state.v1`, deterministic byte-stable), diff (`sicnu.asset_state_diff.v1`), teaching view-model, typed errors.
- **Layer 1 — facts adapters** (thin, I/O only):
  - `src/scientific_state/gdal/` (lib `sicnu_scientific_state_gdal`, links GDAL): `GdalStateFactsCollector` — one read-only open, metadata queries only, never pixel scans; mirrors `canonical_metadata` conventions. Also adapts the Qt-free `sensor_profile` loader output.
  - `src/scientific_state/catalog/` (lib `sicnu_scientific_state_catalog`, links `Sicnu::data`): `CatalogStateFactsAdapter` — `AssetSnapshot` + `RasterStructure` + `DerivationRecord` → facts; classifier sidecar → `ModelDerivedFacts`.
- **Layer 2 — surfaces**:
  - CLI `passport` command in `src/cli/cli_passport_commands.{h,cpp}` (registered in `kCommands` + dispatch; `CliIO::finish` envelope; `--json` machine mode, `--teaching` human mode, `--diff <file>` before/after).
  - Agent read-only tool `data:asset_passport` via `ToolProvider` (added only if registry gates permit — see recon R3; fallback is CLI JSON + library API, documented).
  - Teaching view-model is Qt-free; GUI wiring point documented in `docs/integration.md` (future panel action next to `asset_catalog_index`).

### Claim lattice

`ClaimKind { Known, Inferred, Assumed, Unknown, Conflicted }` — with a strict evidence contract:
- **known**: declared by an authoritative on-asset source (GDAL metadata key, catalog record, sidecar field).
- **inferred**: derived by the resolver from another declared fact (band role from sensor profile axis, resolution from geotransform).
- **assumed**: a documented system default applied without a declaration (FSM "missing ⇒ DN"; `SICNU_SAR_STATE_ASSUMED` projections).
- **unknown**: no source and no default; absence is meaningful.
- **conflicted**: two sources disagree — both values kept in `alternatives`, never auto-resolved.

Every claim records `sources` (ordered, deduplicated source tags). Claims serialize as an id-sorted `claims` array; query-time `claimFor(state, path)` synthesizes `{kind: unknown}` only when neither value nor claim exists — synthesis never serializes.

### Radiometric normalization (projection, no behavior change)

One case-insensitive table: `digital_number|dn`, `radiance`, `toa_reflectance`, `surface_reflectance|boa_reflectance`, `brightness_temperature`, `sigma0|gamma0|beta0` (SAR backscatter family, `domain` recorded separately), plus `SICNU_NUMERIC_SCALE` projection. Cross-checks project existing semantics: `SICNU_SAR_CALIBRATION` vs `SICNU_RADIOMETRIC_STATE` disagreement ⇒ Conflicted (mirrors `readDeclaredSarState`); absent optical marker ⇒ `assumed: digital_number` (mirrors FSM default, made explicit). SAR domain/state values are **projected verbatim** — no dB/linear interpretation (issue #1147 untouched).

## Public API / data schema (essentials)

- `enum class ClaimKind`; `struct ClaimRecord { kind; std::vector<std::string> sources; std::string note; std::vector<std::string> alternatives; }`
- `struct BandState { index, name, role, roleKind, wavelengthNm?, fwhmNm?, units, dataType, noDataValue?, scale?, offset?, radiometricUnit?, maskBand }`
- `struct RemoteSensingAssetState { schemaId; assetId?; revision?; kind; displayName; persistence; lifecycleState; sensor{...}; acquisition{time?, timeSource?, precision}; bands[]; geometry{crs{wkt,authid,geographic,projected}, geoTransform?, pixelSize, size, extent}; validity{noDataPolicy, qualityMaskInfo, cloudCover?}; radiometric{unit, kind, domain?, numericScale?, sources}; temporal{collectionRefs? (bounded)}; provenance{algorithmId?, inputs[], completedAtUtc?, fingerprint?, workflowRef?}; modelDerived{modelKind, labels[], accuracy?, sidecarPath?}; confidence{overall}; assumptions[]; unknowns[]; claims[] }`
- `toJson(state)` — byte-deterministic (fixed key order, id-sorted arrays); `fromJson(json, &state, &error)` — schema-checked, typed error `{code, message}`.
- `resolveState(input) → ResolveOutcome`; `diffStates(before, after) → StateDiff`; `renderTeachingSummary(state) → TeachingSummary` (+ `toPlainText`).
- Constants: `kAssetStateSchemaId = "sicnu.asset_state.v1"`, `kAssetStateDiffSchemaId = "sicnu.asset_state_diff.v1"`.

## Migration / compatibility

Zero changes to existing code paths in slices A–E. F adds: one CLI registration (2 lines + CMake), new `src/scientific_state` subdirs in root CMake (2 lines), optional agent provider registration. No existing key/writer is modified; readers are additive.

## Observability

CLI prints the passport with claim kinds; every resolver inference emits a `ResolutionNote {code, path, detail}` which surfaces in JSON (`notes[]`) so both student and agent see *why* a field is inferred/assumed/conflicted.

## Security / trust boundary

Sidecar JSON parsed with `stackLimit=128` + `Json::Exception` catch (repo convention); passport never executes content; `fromJson` rejects foreign/unknown schema ids with typed errors; paths are recorded, never fetched.

## Performance budget

Resolution is O(bands + facts): metadata only, no pixel reads (enforced by only using metadata APIs in the GDAL collector). Band vector capped at `kMaxPassportBands = 4096` with an explicit `unknowns` note when truncated. Temporal refs capped (paged field `truncated`). JSON output bounded (< 1 MiB for the cap). Diff O(total fields). No caching, no store.

## Test strategy

- Lane 1 (`sicnu_add_sdk_test`, jsoncpp only): slices A–E — value types, round-trip byte determinism, resolver lattices (known/inferred/assumed/unknown/conflicted), radiometric table, diff, teaching view, error paths, invalid/unsafe inputs, boundary (empty bands, cap), deterministic replay (same input twice ⇒ identical bytes).
- Lane 2 (`sicnu_add_io_test`, +GDAL): GDAL collector against in-memory synthesized datasets (MEM/GTiff driver): declared keys, missing keys, per-band metadata, CRS variants, LUT-free SAR keys.
- Lane 3 (one targeted `sicnu_add_test` run at the end): catalog adapter against `AssetSnapshot`/`DerivationRecord` real types.
- Slice G: cross-asset fixture contract tests (Landsat / Sentinel-2 / MODIS / SAR / model-derived) asserting the full projected passport per family, including teaching-mode vs agent-mode semantic consistency (same claims in both renderings).
- Adversarial reviewer pass per slice: fake-green hunts (oracles that can't fail), boundary/encoding attacks (dup keys, wrong schema id, non-finite numbers, over-cap bands), scientific-semantics checks.

## Work packages

See `slices.md` (A–G). Commit granularity: one logical slice per commit.

## Rollback / kill-switch

Entirely additive new directory + 2-line registrations. Kill switch = revert the branch; no data migration exists. The agent tool (if added) is one provider registration removable independently.

## Definition of Done

1. Slices A–G green in their lanes; deterministic replay tests included.
2. End-to-end teaching scenario demonstrable via CLI (`passport --teaching` on a synthesized GTiff) showing declared vs inferred vs assumed vs missing.
3. Machine-readable surface: `passport --json` output validates against `sicnu.asset_state.v1` and round-trips byte-identically; agent tool registered or fallback documented.
4. No silent fallback: unknown/assumed/conflicted always typed; reviewer confirms.
5. Docs: `docs/scientific-state/overview.md`, `schema.md`, `examples/` (2 passports), `docs/integration.md` wiring points.
6. Dynamic dedup re-run before PR; targeted regression on touched lanes; two review rounds.
