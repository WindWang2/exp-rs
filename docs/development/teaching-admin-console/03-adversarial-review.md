# Adversarial review

## Attacks tried (design)

1. **Curriculum cycle** — Kahn check fails closed (`cyclic_prerequisites`).
2. **Missing lab ref** — `unknown_lab_reference`.
3. **Unknown operator / alien LabSpec field / illegal param** — errors.
4. **Malformed artifact kind / unsupported assertion / weight ≠ 1.0** — errors.
5. **Pack `../` traversal** — `path_traversal`.
6. **Corrupt submission in batch of N** — isolated `corrupted` row; batch continues.
7. **Cancel** — remaining rows marked cancelled; `cancelled_early`.
8. **Missing evidence scored 0 fail** — rewritten to `unavailable` (not silent zero).
9. **Regrade** — byte-identical report digest on identical inputs.
10. **Cross-student token in feedback** — `assertNoCrossStudentLeak`.

## Fixes applied during review

- Orchestrator rewrite for missing-evidence silent zero.
- Deterministic row sort before digest/publish.
- Atomic `QSaveFile` publish for JSON+CSV.
- Non-ASCII submission directory covered in tests where FS allows.

## Still out of scope

- Replacing OutputVerifier / process grader kernels.
- Network pack download.
- Editing `#1237` / `#1238` trees.
