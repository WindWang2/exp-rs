# PLAN — eo-ai-model-runtime-foundation-10

Execution order (each numbered step = one coherent commit or commit group):

1. `.gitignore` whitelist + planning scaffolding (this commit).
2. F-OPS-5: spatial-bin NMS + cancellation predicate through dedup/NMS; regression
   tests (100k non-overlapping boxes bounded-time; cancellation honored mid-NMS).
3. F-OPS-1: class_mapping output-encoding upper bound (validate against sentinel +
   promote writeType/NoData from the PRODUCT class domain); regression test.
4. F-OPS-2: TensorBlob::fromMat ND non-continuous handling; regression test.
5. WP-A: EO task taxonomy (closed vocabulary `segmentation|detection|classification|
   change_detection|regression|instance_segmentation|embedding|super_resolution`,
   legacy strings accepted as aliases) + `eo` manifest section (wavelength_nm ranges
   per band role, calibration requirement, grid/CRS assumption) + validation +
   `eoManifestJson()` projection.
6. WP-B: rs:classify / rs:change / rs:regress adapters + instance decode postprocess;
   typed output artifact contract in result payloads.
7. WP-D: preprocess `offset`; postprocess `morphology` (open/close, mask/labels) +
   `probability_calibration` (temperature); effective-contract recording in sidecar.
8. WP-C: TensorRT/OpenVINO optional adapters (CMake auto-detect, default-off impact);
   plugin provider seam docs + SDK projection.
9. WP-F: model-selection knowledge projection (capability JSON + catalog accessors
   for deterministic grade/output meaning).
10. WP-E: tiled-inference resume checkpoint sidecar + skip-completed + provenance note.
11. WP-G: MLOps seam — benchmark record schema + promotion evidence via model digest;
    provenance verification wiring point.
12. WP-H: python/external provider security audit + fixes.
13. Docs: ADR, README bullet, CHANGELOG, docs/processing model docs.
14. Independent review (2 read-only subagents) → fixes → REVIEW_LOG.
15. Final verification at HEAD → push → PR.
