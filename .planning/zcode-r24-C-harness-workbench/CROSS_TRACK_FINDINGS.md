# CROSS_TRACK_FINDINGS — R24-C

## CT-C1 (→ scripts/delivery owner, mirrors r23-D CT-D1)
`scripts/windows/run_lab.cmd` step 3 still grades with `--lab ndvi_basics` (fixture-derived rules) while steps 1-2 produce the foundry 256×256 EPSG:32648 scene → scores 45/100 on master. r23-D added the foundry authority `data/labs/grading/ndvi_landsat.rules.json` (merged #1717). Suggested one-line fix: step 3 `--lab ndvi_landsat`. NOT fixed by C (not a C E2E blocker; scripts ownership). Repro: see .planning/zcode-r23-D/CROSS_TRACK_FINDINGS.md CT-D1.

## CT-C2 (→ teaching/D track): #1695-5
`src/teaching_admin/batch_assessment.cpp` `publishBatchOutputsAtomic` writes JSON and CSV as two separate atomic writes; CSV failure leaves JSON published → generational split between the two outputs. Read-only for C (grading backend). Repro: make CSV target unwritable after JSON write.

## CT-C3 (→ A/B): #1628-3 temporal phenology tool surface unwired
spatial_tools/temporal_spatial_tools.* not registered into SpatialToolRegistry; ingestSeries has no production caller (DECISIONS D-160-1 declared the follow-up). C leaves it (data-side integration decision belongs to the temporal/data owner).

## CT-C4 (information): #1699 conversion pattern
This track converts the 8 C-assigned wall-clock assertions using tests/sicnu_test_time_budget.h (r23-E three-way pattern). Remaining D-assigned file: tests/test_model_failure_matrix.cpp:1000 handled here as C/D-shared per r23-E table; if D track also converts it, keep union (no deletion).

## CT-C5 (→ A track): repair-engine DataType/Dimension rules need an operator
`workflow_repair_engine.h:16-19` has no closed-form rule for DataTypeMismatch ("no registered operator performs dtype conversion" — verified against the full rs:/gdal: operator id enumeration at 5fe642ec6) and DimensionMismatch is not emitted by the checker today. If A adds a deterministic dtype-cast operator (e.g. gdal:translate -ot semantics as an RSOperator), the repair table can close both kinds.

## CT-C6 (→ teaching/D track): batch discovery walked twice per GUI run
r24-c moved the teaching-admin preview scan off the GUI thread (source of truth: src/app/teaching_admin/teaching_admin_dock.cpp, "#1695-3"), but `runBatchAssessment` re-discovers internally, so the submissions directory is scanned twice per batch (two bounded walks; the first posts the pre-run refusal preview). A `discoveredSubmissions` seam in `BatchAssessmentConfig` (src/teaching_admin/batch_assessment.h — read-only for C) would let the GUI pass its list and halve network-share traffic.

(Updated as work proceeds.)
