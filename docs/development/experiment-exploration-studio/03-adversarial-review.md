# Adversarial review — Experiment Exploration Studio

Date: 2026-09-22 (Asia/Shanghai)

## Attacks tried → outcome

| Attack | Result | Fix / residual |
|--------|--------|----------------|
| Illegal dimension range (min>max) | Core `validate` fails; designer `draftValid=false` | Covered by test |
| Combo explosion (50×50 grid, maxRuns=10) | `budget_exceeded` / combo_explosion issues | Covered |
| Spatial grid mismatch | `study.spatial_mismatch` → `gridMismatch` + alignment hints; **no resample** | Covered |
| Nodata/NaN in buffer summary | `validPixels < totalPixels` | Covered |
| Uncertainty with seed_replicates=1 | `uncertaintyMissingReplicate` flagged | Covered |
| Fault original fingerprint change | `sandboxUnchangedOriginal=false` + issue | Covered |
| Student diagnosis ≠ system | `diagnosisMismatch` | Covered |
| Debugger incomplete + high confidence | Forced downgrade to `low` + `confidenceDowngraded` | Covered |
| Export foreign schema | `export_schema_mismatch` refuse | Covered via fromJson |
| Silent truncation of points | Never; policy + core refuse | By design |
| Private thread pool | None; cancel message only | By design |
| Touch teaching paths | Not modified | Boundary held |
| Wipe shared CMake/main_window via ours/theirs | Append-only union | Boundary held |

## Regressions added

- Tests assert mismatch refusal path and confidence downgrade (would catch a silent "best effort overlay" or "always high confidence" regression).

## Known limits (honest)

1. UI Run Matrix demo uses synthetic StudyReport — real TaskCenter runs go through existing study_bridge when wired by host.
2. Class-transition spatial mode is enum-ready; transition table compute stays with future reuse of classification kernels (not reinvented here).
3. Verifier/grader full report merge is export-pointer level in this slice (fault scenario may embed verifier JSON echo).
