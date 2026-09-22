# Teacher Authoring & Assessment Console

Offline teacher workflow over the existing curriculum / LabSpec / pack / grader / verifier / offline-bundle chain. **Projection + orchestration only** — no second truth.

## Workflow (DoD)

1. **Course Builder** — edit `sicnu.curriculum/1` (modules, lab refs, DAG prereqs, outcomes, teacher notes); live schema + cycle + missing-lab checks; student course-home **preview**; canonical JSON export + diff.
2. **Lab Authoring** — edit LabSpec; operators from real registry; params from schema; unknown operator/field = error; recipe compile view read-only.
3. **Rubric Builder** — process dimensions (`sicnu.grader.rubric/1`) and/or artifact assertions (`sicnu.lab.rules/1`); only kinds graders support; live weight sum; no hidden formulas.
4. **Data Pack Manager** — inventory, checksum, CRS/grid summary, byte budget; path-traversal refused; structured adapter to `scripts/gen_lab_packs.py`.
5. **Teacher Dry Run / Preflight** — aggregate versioned `sicnu.teaching_release_report/1`.
6. **Offline Release Builder** — drives `scripts/build_offline_bundle.sh` + `verify_bundle_manifest.py` (controlled subprocess).
7. **Batch Assessment** — import class submission dir; batch verify/grade; typed failures; cancel; one bad row cannot kill the batch; missing evidence ≠ silent zero; CSV/JSON atomic publish; regrade traceability.
8. **Feedback Pack** — per-student overall / dimensions / verifier findings / missing evidence / reproducibility / teacher comment placeholder; no cross-student leak.
9. **Class Summary** — completion, common errors, score distribution, missing-evidence dist; local only.

## Architecture

```
data/curriculum + data/labs + packs + grading
        │
        ▼
 src/teaching_admin (Qt Core)  — validate / orchestrate / report
        │
        ├── script_adapters → gen_lab_packs / build_offline_bundle / verify / classroom_batch
        │
        ▼
 src/app/teaching_admin/TeachingAdminDock  — tabs A–I
```

Parallel boundaries: does **not** touch `#1237` `src/teaching/**` or `#1238` `src/experiment_studio/**`.

## Open the dock

Window menu → **教学作者与评测控制台** (`teachingAdminDock`).
