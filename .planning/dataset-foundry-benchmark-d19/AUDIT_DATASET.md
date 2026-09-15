# AUDIT_DATASET — current-state architecture map

## Primary question

> What is the authoritative representation of a remote-sensing dataset used for scientific training, evaluation and reproducibility?

**Answer (as of ebcafb4d):** A **committed `DatasetVersionRecord`** whose content identity is a fingerprinted **`DatasetManifest`**, with samples / annotations / splits / leakage reports / facets living as rows in **`DatasetStore`** (SQLite WAL). Experiments pin `datasetVersionId` + `splitManifestId` + fingerprints; metrics bind an **`EvaluationProtocol`**. There is **no** first-class Benchmark Definition / Benchmark Runner / FeatureSet join layer yet.

## Fragmentation map

| Concept | Current home | Status |
|---------|--------------|--------|
| Workspace / DataManager / display layers | `src/app`, `src/data` | Display/workspace — **not** scientific dataset authority |
| DatasetStore | `src/dataset/dataset_store*` | **Authority** for versions/samples/splits/schemas |
| LabSpec / auto-grading | lab tracks | Consumes datasets; not a second store |
| Experiment / Run | `src/experiment` | Pins dataset/split; does not own samples |
| Feature artifacts (temporal region tables, etc.) | operators (`rs_temporal_region_features_*`) | Produce tables; **no typed FeatureSet ↔ sample join** |
| Classification labels / RsClassDef | classification studios | Interop via LabelSchema `legacyIntId` |
| Model manifests | model runtime | Promotion reads experiment evidence |
| Benchmark fixtures | ad-hoc tests / labs | **No formal BenchmarkDefinition** |
| STAC assets | geospatial/stac | Source assets referenced by `SourceAssetRef` |
| File paths | various | Manifests pin asset ids + revisions, not raw paths as identity |

## Version DAG

Already implemented:

- `createDraftVersion` → `stageVersion` → `commitVersion` → `deprecateVersion`
- `createDerivedVersion(parent)`, `versionAncestors`, `versionChildren`
- Parent must resolve in-dataset; cycles → `dataset.version_cycle`
- Committed versions immutable

**Gap:** typed **dataset role** (source / derived / training / benchmark / evaluation) not on Manifest — roles today are informal tags/notes.

## Sample model

`SampleKind`: Point, Pixel, Window, Patch, Polygon, Object, Pair, Temporal, MultiModal. References windows/geometry; no inline pixels. Stable ids via `SampleId`.

**Gap:** bounded **query/filter catalog API** beyond `samplesPage` + facets (class/sensor/year/region/quality/split filters as a single query object).

## Label schema

Versioned ontology with hierarchy, ignore/unknown/background, aliases, explicit `LabelMapping`. Pseudo/model labels require model_id/digest/threshold in annotation `sourceDetail`.

**Gap:** benchmark policy to **refuse pseudo-labels in protected test** as a first-class rule object (logic exists piecemeal).

## Splits & leakage

Authoritative engine in `split.h` (Random → SpatioTemporalBlock). Leakage audit with typed kinds and cross-split findings. Store persists manifests + reports immutably.

**Do not** create a second split implementation. D19 generalizes **consumption** (benchmark modes: cross-region/year/sensor) over existing methods.

## Patch extraction

`patch_generator` produces specs + config hash; GDAL-free; caller supplies reader for NoData.

**Gap:** thin **foundry hooks** documenting how patches attach to draft versions (promotion paths exist).

## Feature tables

Operators emit CSV/tables; **no** `feature_set_id` / schema_version / sample_key / digest / refuse-ambiguous-join contract in `src/dataset`.

## Evaluation & experiment

`EvaluationProtocol` binds dataset version + split + subset + ignore labels. Metrics: OA, Kappa, F1, IoU, RMSE, AP, etc. — single authority in `evaluation.*`. Promotion criteria can require benchmark dataset versions (string list only).

**Gaps:**

1. Formal **BenchmarkDefinition** (task family, metric defs, leakage policy, seed/determinism, env pins, allowed preprocessing).
2. Headless **BenchmarkRunner** producing evidence + experiment linkage.
3. Structured **benchmark comparison** (mean/std across seeds) beyond run comparison.
4. Normalized **MetricResult** envelope (name/definition/version/value/scope/support/warnings) for agent/Workbench consumption.

## Dataset QA

Composition + label QA + imbalance + quality cache exist. Missing unified **multi-category QA report** (assets, CRS, bands, duplicates, provenance, split leakage, pseudo-label presence) with PASS/WARN/FAIL/UNKNOWN per category.

## Agent surface

No `dataset:inspect` / `benchmark:*` tools yet. D19 should expose **bounded summary APIs** (FoundryService / BenchmarkService) that agent tools can wrap later — without dumping million-row catalogs into LLM context.

## D18 boundary

Keep all new code under `src/dataset/**` and `src/experiment/**` (+ tests + planning). No Workbench/MissionContext edits.
