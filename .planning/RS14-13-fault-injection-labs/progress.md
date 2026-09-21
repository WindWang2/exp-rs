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
