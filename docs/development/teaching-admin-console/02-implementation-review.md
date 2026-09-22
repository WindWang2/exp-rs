# Implementation review (author)

## Coverage vs goals A–I

| Feature | Status |
|---|---|
| A Course Builder | validateCurriculum + export/diff + course-home preview VM |
| B Lab Authoring | validateLabSpec + recipe compile view |
| C Rubric Builder | lab rules + process rubric validators |
| D Data Pack Manager | inventoryPacks + traversal + budget |
| E Preflight | runPreflight → teaching_release_report |
| F Offline Release | script adapters (build + verify) |
| G Batch Assessment | discover + runBatchAssessment + atomic publish |
| H Feedback Pack | buildFeedbackPack + leak check |
| I Class Summary | buildClassSummary |

## Adversarial notes addressed

- Missing evidence coerced away from silent zero in orchestrator.
- Path traversal closed for pack inputs.
- Canonical digests for release report + regrade identity.
- Subprocess adapters return typed exit/findings — no log scrape.
- Parallel file ownership respected (`teaching_admin` only).

## Residual limits

- Dock dry-run grader is a local stub; production batch should point at CLI / `run_classroom_batch.py`.
- Live operator registry probe is allow-list injected in UI/tests; shell can later wire `RSOperatorRegistry`.
- Full offline bundle assembly needs a built `sicnu_geo_rs_cli` tree.
- Offscreen GUI smoke not automated in this lane.
