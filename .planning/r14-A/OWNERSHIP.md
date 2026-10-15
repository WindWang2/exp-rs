# R14-A Ownership

Writable (production): `src/analysis/**`, `src/processing/algorithms/**`, `src/operators/rs/**`, `src/processing/providers/**`.
Writable (tests): new `test_r14_A_*` files if needed; new TEST_CASEs inside existing science test files (preferred — zero
`tests/CMakeLists.txt` churn while #1504 holds that file).

Read-only for this track: everything else, notably `src/geospatial/**`, `src/data/**`, `src/dataset/**`, `src/experiment/**` (#1504),
`src/operators/runtime/**` (B), `src/agent/**` (C, cartography dirs D), `src/app/**` (D/E), `src/core/**` (unclaimed by prompt —
treated read-only; radiometric_state.* fixes stay with the owning track), `src/science_context/**`, `src/core/temporal_cube.cpp` (#1505).

High-conflict short-lease files: `tests/CMakeLists.txt`, root `CMakeLists.txt`, `.github/workflows/**`, `scripts/**` — avoid; if
unavoidable, tiny isolated commit + explicit ledger note. Current plan: NO changes to any of them.

File exclusive assignment inside this track (single implementer = main agent; audits were read-only subagents):

| Path | Role |
|---|---|
| src/operators/rs/rs_temporal_index_operator.cpp | WP-A1 implement |
| src/processing/algorithms/spectral_local_rx.cpp | WP-A2 implement |
| src/operators/rs/rs_sam_classify_operator.cpp | WP-A3 implement |
| src/operators/rs/rs_continuum_removal_operator.cpp | WP-A4 implement |
| src/operators/rs/rs_radiometric_qa_operator.cpp (+ kernel if needed) | WP-A5 implement |
| src/processing/algorithms/image_fusion.cpp | WP-A6 assess/implement |
| tests/test_d16_temporal_tools.cpp (or temporal parity file) | WP-A1 RED case |
| tests/test_spectral_local_rx.cpp | WP-A2 RED case |
| tests/test_rs_operators.cpp | WP-A3/A5 RED cases |
| tests/test_continuum_removal.cpp | WP-A4 RED case |
