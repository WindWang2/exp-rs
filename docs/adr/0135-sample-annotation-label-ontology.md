# ADR 0135: Sample, Annotation & Label Ontology Model

- Status: Accepted (2026-09-07)
- Scope: `src/dataset` (sample, label, annotation contracts)
- Depends on: ADR 0134 (identity/store), ADR 0065 (semantic band roles)
- Ownership: Track D

## Context

Samples exist today only as in-memory `RsTrainingGeometry`/`cv::Mat` rows in
the classification pipeline, labeled by a UI-relative `int classId`
(`RsClassDef`), with no persistence, no provenance, no confidence and no
revision history. Segmentation labels ride ad hoc in operators. Nothing can
answer "where did this label come from, how confident is it, and how has it
changed". Meanwhile remote-sensing work needs sample shapes the pipeline
never modeled: pixel windows, patches, pre/post pairs, irregular temporal
series, multi-modal stacks.

## Decisions

1. **One shared sample core + typed payloads.** `SampleRecord` carries the
   common envelope (stable `SampleId`, dataset version, source asset refs
   (AssetId+revision), CRS, time, quality, weight, group id, provenance,
   label refs). Payload is a `std::variant`:
   `PointSample` (coordinate), `PixelSample` (pixel index), `WindowSample`
   (half-open pixel window + ground footprint), `PatchSample` (window +
   generator-config hash + border/nodata policy actually applied),
   `PolygonSample` (WKT), `ObjectSample` (segment reference), `PairSample`
   (pre/post member refs, pair role), `TemporalSample` (observation list:
   time+asset+quality+missing flags, target time), `MultiModalSample`
   (modality → member sample refs with per-modality required/optional,
   CRS/grid/resolution expectations, missing policy).
   - Rejected: N independent sample structs per task. Nine payload kinds
     under one envelope keep ids, provenance, storage and paging uniform;
     task-specific fields stay in typed payloads, not QVariantMaps.

2. **Spatial contract is explicit, never silent.** Pixel/map conversions use
   the source asset's geotransform; windows are half-open `[x, x+w)`; ground
   footprints derive from the window; CRS is stored per sample and per
   dataset version; border handling (`drop|clip|pad|reflect|constant`) and
   NoData handling (min valid fraction, mask, drop, keep-with-flag) are
   recorded ON the patch, not re-derived. The module performs NO resample
   and NO reproject: mismatched grids are a validation error, and any future
   harmonization goes through Track A's grid contracts via adapter.

3. **Label ontology with stable identity.** `LabelSchema` (versioned, stable
   `LabelSchemaId`) contains `LabelClass` rows: stable UUID, string `code`
   (unique per schema), display name, description, parent code (hierarchy),
   color, flags (background/ignore/unknown), aliases, metadata. Class
   identity = code+schema version, never a row index. `LabelMapping`
   (named, versioned) recodes between schemas; applying a mapping is a
   recorded dataset-version event, never an implicit query-time rewrite.
   - Rejected: keeping `RsClassDef` int ids as the primary key. Ints are
     kept ONLY as alias metadata for interop with the classification UI.

4. **Annotation provenance and revisions.** `Annotation` (stable
   `AnnotationId`) targets a sample or entry with a label value (class code,
   or WKT + class for segmentation), source type (`human|field_survey|
   existing_map|manual_interpretation|model_assisted|weak|pseudo|
   external_dataset`), source detail (for pseudo: model id + digest +
   threshold + generation config), confidence, review status, reason, and a
   revision chain (`parentRevisionId`, change summary, timestamp, author
   role). Current label = tip of the chain; history is immutable.

5. **Weights and groups are first-class.** Every sample carries `weight`
   (sampling/loss weight) and `groupId` (spatial/temporal/object group used
   by grouped splits and leakage audit). They default but are explicit
   fields, not conventions.

## Compatibility

- Purely additive. The classification pipeline keeps its cv::Mat path; a
  thin adapter (later milestone) lifts extraction results into
  `SampleRecord`s by mapping `RsClassDef` ids through a `LabelMapping`.
- Serialization is schema-versioned JSON in the dataset store; readers
  tolerate unknown fields and reject wrong versions.

## Resources

- Samples persist in the dataset store with index-backed paging; the typed
  payloads serialize compactly (no inline rasters — patches reference
  windows into source assets; payload bytes stay with the artifact layer).
