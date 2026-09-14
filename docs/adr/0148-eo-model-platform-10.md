# ADR 0148 — EO AI Model Runtime / Foundation Model Platform 10.0

Date: 2026-09-14 · Status: Accepted · Track: `eo-ai-model-runtime-foundation-10`

## Context

Model Runtime 9.0 (ADR 0144) delivered the execution seam: manifest 4.0/5.0
identity, provider matrix with real CUDA execution, per-feed preprocessing,
tile engine with feather blending, detection decode, and consumer-side
provenance verification. What master still lacks is the EO DOMAIN layer: the
manifest cannot express wavelength sensitivity, calibration requirements or
CRS-family assumptions as ENFORCEABLE facts; four EO task families
(classification, change detection, regression, instances) have no task-shaped
operator; deployment providers (TensorRT, OpenVINO) have no adapter seam; the
agent cannot choose models from manifest FACTS instead of names.

## Decision

1. **EO truth layer on the manifest** (additive, optional `eo` section,
   `manifest_version: 6`): per-band-role wavelength windows (`wavelengths_nm`),
   a required radiometric calibration (`calibration.state` from the
   platform-wide SICNU_RADIOMETRIC_STATE vocabulary, `enforced` = fail-closed
   preflight), and a CRS-family grid assumption (`grid.crs_family`). Input
   facts come from the canonical metadata layer (`inspectRaster`), never
   guesses; enforced mismatches are typed refusals BEFORE any tile is read,
   and every check/advisory is recorded into run stats + provenance.
2. **Canonical EO task vocabulary** with 1:1 aliases (`canonicalEoTask`);
   task-shaped adapters (rs:classify, rs:change, rs:regress) gate on the
   canonical task so INTENT and manifest contract must agree. Scene
   classification runs ONE forward pass over a bounded scene window and
   publishes the typed `exp-rs-classification/1` artifact.
3. **Manifest-driven pre/post extensions**: preprocess `offset` (after scale;
   refused under normalize "none" — declared knobs are never silently
   ignored), postprocess `calibration_temperature` (probability-space
   temperature scaling before the derived collapse) and `morphology`
   (sentinel-aware streaming cleanup on the published labels/mask product,
   bounded O(W·k) memory, seam-exact via kernel-radius halos).
4. **Optional provider adapters**: TensorRT (prebuilt .engine/.plan,
   GPU-only) and OpenVINO (IR/ONNX) compile ONLY when explicitly enabled AND
   the SDK is found; absent SDKs register typed-unavailable stubs — the
   default build's artifact set and behavior are identical to master.
   `ModelRuntimeRegistry::registerProvider` stays the plugin seam.
5. **In-ownership defect fixes from the whole-repo line review**: F-OPS-5
   (grid-bounded, cancellable NMS — the kept set is bit-identical to the
   historical dense pass), F-OPS-1 (Labels encoding driven by the PRODUCT
   class domain; class_mapping capped below the UInt16 sentinel), F-OPS-2
   (TensorBlob::fromMat copies non-continuous ND Mats byte-exact).
6. **MLOps seam**: `verifyProductAgainstModel(outputPath, modelReference)` —
   the stable accessor for benchmark sets, promotion evidence and replay
   deviation checks (resolution through the catalog's readiness pipeline).
7. **Agent knowledge**: capability-knowledge entries for the new operators
   (data/agent/capabilities) + a model-selection guide so model choice is a
   manifest-facts decision; the manifest round-trips the new fields through
   `ModelInfo::toJson`.

## Consequences

- Old manifests run bit-identically: every 10.0 field is optional and absent
  sections keep historical behavior (tests pin the round-trip).
- The resume seam for tiled inference is DESCOPED (see
  `.planning/eo-ai-model-runtime-foundation-10/DECISIONS.md` D8): reusing a
  partial stage requires an attach-existing contract on GdalStreamingOutput
  (geospatial-io foundation); the 100000px extent scale + cancellation
  evidence ships instead.
- The TensorRT/OpenVINO execution paths are compiled only on deployment
  hosts; on hosts without the SDKs they are typed refusals (capability-gated
  tests SKIP honestly rather than fabricate).
