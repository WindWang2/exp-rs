# RS14-01 Scientific Data Passport — progress

## Slice C — geometry/validity/temporal projection (2026-09-22)
- RED: new `tests/test_scientific_state_geo.cpp` (22 cases) failed to compile —
  `GeometryFacts` / `DatasetFacts::geometry` / `CatalogFacts::temporalRefs` did not
  exist (capability missing, not fixture-broken).
- GREEN (minimal):
  - `state_facts.h`: new `GeometryFacts` (hasCrs/wkt/authid/flags, hasGeoTransform
    + 6-tuple, width/height; size present iff w>0 && h>0), attached as
    `DatasetFacts::geometry`; `CatalogFacts::temporalRefs`.
  - `asset_state_resolver.cpp`: `resolveGeometry` (CRS known claim "geometry.crs"
    source "gdal:CRS", missing CRS ⇒ typed unknown; pixelSize |gt1|,|gt5| inferred
    + note `geometry.pixel_size_from_geotransform`; extent = 4-corner bbox incl.
    rotation gt2/gt4, inferred + note `geometry.extent_from_geotransform`); 
    `resolveValidity` (noData policy declared/undeclared/partial by band census,
    CLOUDCOVER known/invalid⇒unknown+note `validity.cloud_cover_invalid`,
    QA vocab SICNU_QA_VOCABULARY known > profile inferred+note
    `validity.qa_from_sensor_profile`); temporal refs sorted, >256 truncated +
    note `temporal.truncated`, claim "temporal.refs" known "catalog:collection";
    band noData now merges via the evidence lattice (dataset band item
    "NO_DATA_VALUE" + catalog hasNoData ⇒ claim "bands[i].no_data", disagreement
    ⇒ conflicted, unparsable ⇒ unknown + note `validity.nodata_invalid`).
- Decisions/pins: band native NoData is handed to the resolver as band metadata
  item "NO_DATA_VALUE" (Slice F collector must match); dataset-declared noData
  also populates BandState.hasNoData/noDataValue (same logical field, symmetric
  with catalog mirror); 0 bands ⇒ policy stays unset (""), claim unknown.
- Result: test_scientific_state_geo 96 assertions / 22 cases green;
  test_scientific_state_core 76/12, test_scientific_state_resolver 78/18
  unchanged green.

## Slice D — provenance + model-derived projection + sidecar parsing (2026-09-22)
- RED: new `tests/test_scientific_state_provenance.cpp` failed to compile —
  `scientific_state/model_sidecar.h` did not exist.
- GREEN (minimal):
  - New `src/scientific_state/model_sidecar.{h,cpp}`:
    `parseClassifierSidecarJson(text, sidecarPath, out, error)` parses the real
    classifier sidecar (rs_classification_pipeline saveModelSidecarV2 shape,
    versions 1|2) with CharReaderBuilder + stackLimit 128 + try/catch.
    version required integral 1|2 else InvalidField; method required non-empty
    string → modelKind verbatim (unknown methods projected, never refused);
    classes[].id → labels sorted/dedup as decimal strings; validation.
    overallAccuracy → hasAccuracy (absent in v1 = explicit, not an error);
    featureSchema.features[].name → comma-joined presence summary.
    Non-JSON / depth bomb (>128 nesting) ⇒ MalformedJson without crash.
  - `src/scientific_state/CMakeLists.txt`: model_sidecar.cpp added to
    sicnu_scientific_state.
  - Resolver: DerivationFacts → ProvenanceSection (inputs sorted by
    assetId/revision, bandReferences sorted) with claims "provenance.algorithm"
    / "provenance.inputs" known "catalog:DerivationRecord" — claims only for
    content actually carried; unset derivation ⇒ isDerived=false, no claims.
    ModelSidecarFacts → ModelDerivedSection with claim "model_derived.labels"
    known "sidecar:classifier-meta"; empty labels ⇒ typed unknown.
- Refinement during GREEN: 3 assertions originally checked `state.claims.empty()`,
  but a minimal resolution legitimately carries sensor/acquisition unknown
  claims; replaced with `hasClaimUnder(state, "provenance.")` prefix checks
  (tighter semantics, not weaker).
- Result: test_scientific_state_provenance 71 assertions / 15 cases green;
  core 76/12, resolver 78/18, geo 96/22 unchanged green.
