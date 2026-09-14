# OWNERSHIP — hyperspectral-spectral-intelligence-10

## This track owns

* `src/processing/algorithms/`: NEW spectral kernel files (`spectral_table.*`, `mnf_transform.*`) + surgical extensions to `spectral_unmixing.*` (FCLS), `spectral_classification.*` (nothing planned — only if a guard requires), `spectral_library.*` (nothing planned — consume as-is).
* `src/operators/rs/`: spectral operators (`rs:endmember_extraction`, `rs:sam_classify`, `rs:spectral_unmixing`, `rs:matched_filter`, `rs:ace`, `rs:mnf`, `rs:mnf_inverse` (new), `rs:spectral_band_select` (new), `rs:library_select` (new)) + shared operator-layer reference-input seam (`rs_spectral_reference_input.*`) + registrations in `rs_operators_init.cpp` (append-only).
* `data/spectral/`: append-only schema/format additions for the spectral-table artifact (`spectral_table.schema.json`) — library.json/sensors.json stay untouched.
* `tests/`: new/extended spectral test files + CMake entries.
* `.planning/hyperspectral-spectral-intelligence-10/`: track records.
* Docs: `docs/processing/` policy pages where a contract changes (validation-policy table rows), `docs/adr/0147-*` if an ADR is warranted, CHANGELOG entry.

## Explicitly out of scope (other owners)

* Model Runtime / inference operators (`src/operators/runtime/`, `models/`) — Track 08.
* SAR / polarimetric / InSAR — `zcode/advanced-sar-polsar-insar-10`.
* Temporal / phenology — `zcode/temporal-eo-phenology-change-10`.
* Product import adapters, sensor metadata discovery — `zcode/cn-eo-products-sensor-physics-10` (this track *consumes* WAVELENGTH band metadata and `data/spectral/sensors.json`, changes neither's contracts).
* Cloud/datacube fabric — `zcode/cloud-data-fabric-datacube-10`.
* GUI dialogs beyond what operator schema changes force (SchemaFormBuilder renders schemas; no dialog surgery planned). The spectral workbench dialogs keep their behavior.
* Workflow engine mechanics (`src/workflow/*`, `src/processing/framework/task_center.*`) — artifacts ride the existing path/string placeholder contract; any defect found there is recorded, not fixed, unless it blocks this track's acceptance (then: minimal fix + DECISIONS entry).

## Cross-track seams

* If `rs:spectral_*` schemas gain params, the GUI renders them via SchemaFormBuilder with zero app code — no shared UI file edits expected.
* New operators appear in CLI `--list`, MCP `tools/list`, and the Agent Tool Catalog automatically through the Operator Registry — no per-surface edits expected (verify in Phase 4).
