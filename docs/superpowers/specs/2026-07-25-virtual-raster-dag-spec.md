# Spec: Virtual Raster Assets and the Strong-Dependency DAG

**Parent:** `docs/superpowers/specs/2026-07-24-data-manager-architecture-spec.md` — Virtual Raster Assets (lines 324-351), Provenance and Dependencies (lines 167-182), Lifecycle and Destructive Operations (lines 414-422), and the Phase 1 plan's Deferred Follow-Up Order item 4.
**Status:** Proposed.

## Problem Statement

Remote-sensing work constantly composes bands that live in different files: an RGB false-color composite from three single-band rasters, a Landsat band set reassembled after per-band import (#53), a cross-sensor stack. Today the only composition mechanism is the GDAL VRT **file** handled like any other raster — the application has no domain concept of a *virtual raster*. `AssetKind::VirtualRaster` exists in the enum but nothing ever produces it: no recipe type, no composition API, no preflight validation, and — critically — no dependency tracking. A user can build a VRT on disk, register it, then unload one of its source rasters; the VRT silently breaks (GDAL reports a missing source at read time) with no warning at unload time.

The architecture spec already says what this should be (lines 324-351): a cross-file band composition is a **Virtual Raster Asset**, not display-layer multi-source logic; its **recipe** records ordered input Band References, target CRS/grid/resolution, extent policy, resampling, and NoData policy; creation requires a **preflight** returning structured diagnostics; and a virtual asset has **Strong Dependencies** on its inputs that form a directed acyclic graph and **block normal input unload** (line 169). None of this exists.

The user-facing problem: there is no safe way to compose bands across assets. Either the user materializes a stacked raster eagerly (disk cost, duplicated data, no link to inputs), or hand-builds VRTs outside the app's identity model (no dedup, no dependency safety, no project persistence of the composition's meaning).

## Solution

Introduce the **Virtual Raster Asset** as a first-class catalog kind with a serializable **recipe**, a pure **preflight**, a **strong-dependency DAG** in the Data Manager, and a provider that realizes the recipe as a GDAL VRT.

- A **VirtualRasterRecipe** is a value type recording ordered `BandRef` inputs (AssetId + 1-based band number), target CRS, target grid (resolution + extent), extent policy (Intersection default / Union as an explicit advanced choice), per-semantics resampling, and NoData policy. It is the persisted, identity-bearing form of the composition — the `.vrt` it produces is a build artifact, never the source of truth.
- A **preflight** is a pure function over the recipe plus the input assets' immutable snapshots. It returns a structured verdict: `Compatible`, `RequiresReprojection`, `RequiresResampling`, `PartialOverlap`, `NoOverlap`, `MissingCRS`, `UnavailableSource`, or `UnsupportedDataType` — so creation fails (or warns) **before** anything is registered, with diagnostics the UI can present verbatim.
- **Strong dependencies** are recorded in the Data Manager as DAG edges from the virtual asset to each input asset. Cycle detection is enforced at edge-creation time (a virtual raster may consume another virtual raster — chains are legal, cycles are not). Normal unload of an asset with downstream strong dependents is refused, exactly like an active lease; `planUnload` reports the dependents in its impact; confirmed cascade unload removes dependents transitively (deepest first).
- A **VirtualRaster provider** realizes the recipe as a GDAL-readable VRT. The recipe is stored by the Data Manager; the provider builds the VRT XML deterministically from the recipe and the current input snapshots, so relocation of an input (same AssetId, new path) keeps the virtual asset valid after a rebuild — the recipe references AssetIds, not paths.
- **Serialization** persists recipes (not VRT files) into the `.qgz` extension; on project open, recipes are restored, dependencies rebuilt, and VRTs regenerated.

## User Stories

1. As an analyst, I want to compose three single-band rasters into an RGB virtual raster, so that I can display and process the composite without duplicating data on disk.
2. As an analyst, I want the composition to fail before creation when my inputs don't overlap or lack a CRS, so that I never register a broken virtual raster.
3. As an analyst, I want inputs on different grids to be composable when I give an explicit target grid and resampling, so that I control the resampling rather than having it silently guessed.
4. As an analyst, I want the Data Manager to refuse unloading a band that a virtual raster depends on (unless I confirm a cascade), so that I don't silently break my composite.
5. As an analyst, I want a cascade unload to remove the dependent virtual rasters too, so that cleanup doesn't leave dangling broken assets.
6. As an analyst, I want a virtual raster composed from another virtual raster to work (a chain), but a cycle to be rejected, so that layered compositions are safe.
7. As an analyst, I want my virtual rasters to survive save and reopen with their recipes and dependencies intact, so that reopening my project restores my compositions.
8. As an analyst, I want relocating an input file (same asset, new path) to keep the virtual raster working after re-resolution, so that moving data doesn't destroy my compositions.
9. As a developer, I want the recipe — not the VRT file — to be the persisted identity, so that the composition's meaning is independent of any build artifact's location.
10. As a developer, I want preflight to be a pure function over the recipe and snapshots, so that it is unit-testable without GDAL I/O beyond the existing provider resolution.

## Implementation Decisions

- **The recipe is the identity; the VRT is an artifact.** A `VirtualRasterRecipe` value type (ordered `BandRef{assetId, bandNumber}` inputs, target CRS, target grid, extent policy, resampling, NoData policy) lives in `src/data` and serializes to/from JSON (mirroring `DerivationRecord`'s JSON-native approach from #38). The Data Manager stores one recipe per virtual asset. The `.vrt` XML is generated deterministically from the recipe and is never serialized or dedup-keyed.
- **BandRef, not Band Reference strings.** Inputs are `AssetId` + 1-based band number. (The existing `DerivationRecord::bandReferences` QStringList stays as-is — provenance display strings; the recipe's typed BandRef is the composition form. Normalizing band references across both is deferred to #7.)
- **Strong dependencies are a DAG inside the Data Manager.** A `createVirtualRaster(recipe, ...)` call (after a successful preflight) registers the virtual asset and records one strong-dependency edge per distinct input AssetId. Edge creation runs cycle detection (DFS over the existing graph); a cycle fails creation with `dependency.cycle` and nothing is registered. Query API: `strongDependenciesOf(id)` (inputs) and `strongDependentsOf(id)` (consumers). **Normal unload of an asset with strong dependents is refused** with impact diagnostics naming the dependents (mirroring the lease-refusal shape); `planUnload` includes dependents in its impact list; `confirmedCascade()` unload removes dependents transitively, deepest-first, emitting `assetAboutToUnload`/`assetRemoved` per removed dependent. Reaping a temporary asset with dependents follows the same refusal rule.
- **Preflight is pure and returns a verdict + diagnostics.** `preflightVirtualRaster(recipe, manager)` reads immutable snapshots and classifies: `NoOverlap`/`PartialOverlap` (extent intersection), `MissingCRS` (any input without CRS), `UnavailableSource` (input missing/Error), `RequiresReprojection` (CRS differ), `RequiresResampling` (grids differ beyond tolerance), `UnsupportedDataType` (categorical input with continuous resampling requested), else `Compatible`. Verdicts that are hard failures (`NoOverlap`, `MissingCRS` without an explicit target CRS, `UnavailableSource`, `UnsupportedDataType`) block creation; warning verdicts (`RequiresReprojection`, `RequiresResampling`, `PartialOverlap`) allow creation when the recipe carries an explicit target grid/CRS — the recipe's target makes them resolvable. Empty intersection rejects creation by default (spec line 349); union extent requires the explicit `Union` extent policy.
- **Default policies mirror the spec.** Extent policy defaults to `Intersection`; `Union` (with NoData fill) must be chosen explicitly. Resampling defaults to `NearestNeighbour` for categorical inputs and `Bilinear` otherwise; a categorical input with a continuous resampling in the recipe is `UnsupportedDataType`. Target CRS defaults to the common input CRS; when inputs differ, the recipe must carry an explicit target CRS.
- **Provider realizes the recipe as a VRT.** A `VirtualRasterSourceProvider` joins the provider registry. Its `SourceDescriptor` carries a `vrt` provider key and the recipe identity in `dataOptions`; `resolve()` fetches the recipe from the Data Manager's recipe store (the provider is internal, so a back-reference is legitimate — the public interface never exposes it), generates the VRT XML (hand-built, matching GDAL's VRT schema: `VRTDataset` with `VRTRasterBand` per recipe input, `SimpleSource`/`ComplexSource` referencing each input's current canonical source), and returns a `ResolvedSource` with `kind = VirtualRaster`, `Renderable | ReadablePixels` capabilities, and structure read from the generated VRT. The VRT is materialized to a managed scratch location (session-temporary directory owned by the Data Manager) so GDAL can open it; the file is a disposable build artifact regenerated on resolve/reload. Input relocation keeps the virtual asset valid: the recipe holds AssetIds, the provider re-reads current canonical sources at resolve time.
- **Registration reuses the existing pipeline.** `createVirtualRaster` runs preflight, refuses hard-failure verdicts, then registers through the normal `registerSource`-equivalent path (dedup by recipe-derived SourceKey), records strong dependencies, and stores the recipe. `AssetKind::VirtualRaster` is finally produced by a real path.
- **Serialization.** The `.qgz` extension gains a `<virtualRasters>` element: each `<virtualRaster>` carries its AssetId, revision, persistence, and embedded recipe JSON. Restore (mirroring `restoreSource`/`restoreCollection`) re-registers the virtual asset with its original AssetId, rebuilds dependency edges (skipping edges whose input AssetId is absent, with a Warning diagnostic — a missing input yields an `UnavailableSource` asset, not a dropped asset), and regenerates the VRT. Round-trip preserves recipe, dependencies, and identity.
- **Materialization is out of scope.** Converting a virtual raster to a real raster is an Algorithm Task (`gdal_translate` on the VRT) through the existing committer (#39) — no new machinery is needed, and this wave does not wire a UI for it.

### Decision-rich shape (from the design, not a working demo)

```text
BandRef { AssetId asset; int bandNumber; }                 // 1-based
VirtualRasterRecipe {
  QVector<BandRef> inputs;               // ordered — band i of output = inputs[i]
  QString targetCrs;                   // authid or WKT; empty = common input CRS
  double targetResolutionX = 0, targetResolutionY = 0;  // 0 = first input's
  ExtentPolicy extentPolicy = Intersection;             // Intersection | Union
  ResamplingMethod resampling = Bilinear;               // per-semantics default
  NoDataPolicy noDataPolicy = Preserve;                 // Preserve | FillValue
  double noDataFillValue = 0;
} // toJson()/fromJson(), lossless

enum class PreflightVerdict {
  Compatible, RequiresReprojection, RequiresResampling, PartialOverlap,
  NoOverlap, MissingCRS, UnavailableSource, UnsupportedDataType };
struct PreflightResult { PreflightVerdict verdict; bool canCreate;
                         QVector<Diagnostic> diagnostics; };
PreflightResult preflightVirtualRaster( const VirtualRasterRecipe &,
                                        const DataManager & );

// DataManager additions (no interface widening beyond these)
Result<AssetId> createVirtualRaster( const VirtualRasterRecipe &,
                                     PersistencePolicy = ProjectPersistent );
std::optional<VirtualRasterRecipe> virtualRasterRecipe( AssetId ) const;
QVector<AssetId> strongDependenciesOf( AssetId ) const;   // inputs
QVector<AssetId> strongDependentsOf( AssetId ) const;     // consumers
```

## Testing Decisions

- **The seam is the DataManager.** Recipe, dependencies, cycle detection, unload blocking, cascade removal, and serialization round-trip are tested through the DataManager interface, mirroring `test_data_manager.cpp` / `test_data_manager_collection.cpp`. Preflight is tested as a pure function against registered fixture rasters (staged copies of `dem_sample.tif` plus a second-grid fixture created in-test via GDAL).
- **External behavior only:** preflight classifies each verdict against staged inputs (same-grid/same-CRS → Compatible; disjoint extents → NoOverlap; different CRS → RequiresReprojection; missing-CRS input → MissingCRS; unloaded-then-referenced input → UnavailableSource); cycle creation is rejected (A←B←A); normal unload of a depended-on input is refused with dependents named; cascade unload removes the virtual asset; a chained virtual raster (virtual of virtual) works; save/reopen restores recipe + dependencies; a relocated input keeps the virtual asset resolvable.
- **Display integration** is verified by adding the virtual asset to a Display View through the existing `QgisDisplayManager` (the .vrt opens as a normal GDAL raster), not by testing renderer internals.
- **Prior art:** `test_derivation_record.cpp` (JSON value type), `test_data_manager_collection.cpp` (catalog node + lifecycle), `test_data_project_roundtrip.cpp` (serialization), `test_output_committer.cpp` (registration pipeline).

## Out of Scope

- **Virtual Raster creation and preflight UI.** The parent spec defers it (line 527). This wave is the data model, DAG, preflight, provider, and serialization. A band-composition dialog is a follow-up wave once the model is proven.
- **Materialization of a virtual raster to disk** as a user-facing command (it is already possible via `gdal_translate` + #39; no new work needed here).
- **Value-domain transformation and normalized spectral metadata** (#7). The recipe carries resampling/NoData policy only; calibration semantics (TOA/BOA/temperature) are a later wave.
- **Derived-cache integration for virtual rasters** (statistics/histogram of a virtual band) — the derived cache is a separate deferred item.
- **Live re-resolution on input reload.** When an input's revision advances (explicit reload), the virtual asset's derived metadata invalidation is noted but automatic dependent revalidation (spec line 422) is deferred; the virtual asset re-resolves on next open/relocation.
- **Cross-collection or cross-project virtual rasters.** Inputs must live in the same project's catalog.

## Further Notes

- This wave makes the parent spec's lines 324-351 and 169 actually true, and it is the first introduction of a **dependency relationship** between assets — the DAG representation and unload-blocking rule set the precedent for every later dependency kind (materialization provenance links deliberately do NOT block unload; only strong dependencies do).
- The provider back-reference to the recipe store is the first provider that is not stateless-per-source. Keeping that seam internal (the public `SourceDescriptor` carries only provider key + options) preserves the rule that callers never learn provider internals (parent spec line 252).
- The no-widening discipline applies in reverse here: `createVirtualRaster` + the two dependency queries are new public interface because the second real caller (the creation UI, next wave) requires them — the DAG exists precisely so that caller can plan composition and unload safely.
- Wave ordering within this spec: (1) recipe value type + preflight verdict types, (2) strong-dependency DAG with cycle detection + unload blocking/cascade, (3) preflight pure function, (4) VirtualRaster provider + `createVirtualRaster` end to end, (5) serialization round-trip. Each is a small commit; (3) is testable with staged fixtures before (4) generates a real VRT.
