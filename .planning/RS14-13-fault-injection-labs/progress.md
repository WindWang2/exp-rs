# Progress — RS14-13-fault-injection-labs

## 2026-09-21
- Phase 0 recon complete (recon.md): no open PRs; open issues match the avoid-list; lab platform (LabSpec/OutputVerifier/lab_diagnostics/wrong_answer_corpus) is the reuse surface; gaps = fault transforms + scenario schema + delta model + runner.
- Phase 1/2: plan.md + slices.md written (12 fault families incl. split variants; 13 exemplars planned).
- Slice A (scenario schema gate, sandbox contract, deterministic core types): RED tests written first, 15/15 green.
  - New: src/faultlab/{fault_types,fault_registry,fault_sandbox,fault_scenario,deterministic}.{h,cpp}, util/{sha256,canonical_json}.{h,cpp}
  - New: data/faultlab/faults.schema.json (sicnu.lab.faults/1), tests/test_faultlab.cpp, CMake wiring (2 root lines + appended test block)
  - Committed as slice A.
- Slice B (metadata/state faults): RED tests first, 21/21 green.
  - New: fault_transforms.{h,cpp} (band_role_swap, omit_quality_mask, wrong_scale_offset, nodata_as_data + closed param validation + typed refusals), fault_observables.{h,cpp} (band_count/band_roles/crs/grid/nodata+valid_fraction/per-band stats/index_mean/leakage/threshold+kappa/channel_order/model_output/provenance), fault_expectations.{h,cpp} (Changed/DeltaGe/DeltaLe/Equals/NotEquals/InRange/TruthIs with evidence + typed missing-observable failures).
  - Fixed during TDD: registry optional param metadata; mutations counts; index fixture design (constant bands so the swap delta is exact).
- Slice C (geometry/temporal faults): RED tests first, 25/25 green.
  - New transforms: grid_shift (image-space pixel offsets × pixel size, sign documented), crs_mismatch (closed CRS vocabulary, same-CRS refused as non-fault), temporal_shuffle (purpose-derived seed Fisher-Yates, identity-draw retry + rotate fallback so the fault always moves), temporal_gap (integer index, range-checked, date-carrying target).
  - Uniform TransformFn signature (grid, params, seed) so stochastic and deterministic transforms share one dispatch table.
- Slice D (ML/evaluation faults): RED tests first, 28/28 green.
  - New transforms: train_test_spatial_leakage (duplicate clones train points into test role; relocate moves a test point onto a train coordinate — both move leakage.overlap_fraction with typed evidence), threshold_misuse (closed [0,1] range, same-threshold refused as non-fault), model_channel_mismatch (bijection-checked permutation of the declared channel order; weights stay put so model_output_mean moves by an exactly computable delta).
  - Fixed during TDD: relocate semantics = test point moved into train region (test_count unchanged); model output arithmetic (13.1); duplicate overlap = 0.5 (clones overlap, originals do not).
- Slice E (artifact/provenance fault + report schema): RED tests first, 30/30 green.
  - provenance_removal: scope generator (blanks generator+seed, observable flips true→false) / all (block removed, observable disappears as typed absence); refusals for unknown scope and provenance-free fixtures.
  - fault_report.{h,cpp}: sicnu.faultlab.report/1 canonical body (no timestamps/paths), digest-stable across runs, parseable, verdict + evidence + diagnostics sections.
