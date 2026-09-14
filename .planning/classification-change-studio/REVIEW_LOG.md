# REVIEW_LOG — D15 Classification & Change Detection Studio

## Round schedule

- Round 1 (Phase 6): dual-axis review — Axis 1 Standards (C++20 modernity,
  Qt6 thread/lifecycle discipline, Karpathy minimalism), Axis 2 Spec
  (leakage-freedom, probability conservation, bit-exact streaming,
  NoData/epsilon guards).  Reviewers: ≤3 read-only subagents.
- Findings triaged P0 (veto/blocking), P1 (must fix), P2/P3 (backlog).
- Exit: P0 = 0, P1 = 0, full suite green, rubric self-score >= 90/100.

## Rubric self-assessment (filled in Phase 7)

| Dimension | Weight | Score | Notes |
|---|---|---|---|
| Public seams & contracts | 20 | TBD | |
| Vertical slices & tracer discipline | 20 | TBD | |
| Ground-truth independence | 20 | TBD | |
| Black-box decoupling | 20 | TBD | |
| YAGNI & standards | 20 | TBD | |
| **Total** | **100** | **TBD** | |

## Findings

(TBD — Phase 6)

## Deviations register (declared up front, reviewed in Phase 6)

1. **Spec prose vs spec test, magic-wand polygon**: the spec prose mentions a
   convex hull; the spec's own acceptance window ([1240, 1270] px around
   pi*20^2 with zero background leaks) is only satisfiable by a
   boundary-faithful outline, so the wand returns the crack-following
   boundary of the accepted mask (DECISIONS G2).  The test is the contract.
2. **MPL pipeline CVA alpha = 4.0** (not the 1.5 default): unchanged-pixel
   CVA magnitudes are chi(4)-distributed; 1.5 sigma sits inside the fat tail
   (~3% false alarms, Dice ~0.6).  Calibrated to 4 sigma (~5e-6) —
   DECISIONS I1, visible in `d15_e2e_pipeline.cpp`.
3. **Build-cycle pipelining**: the repo-wide baseline build (6259 ninja
   targets at -j2) blocks any test execution for hours.  Red/green was kept
   strictly sequential *per package* (stub red run -> implementation green
   run -> atomic commit, A->B->...->I), but green implementations were typed
   ahead during the baseline build window.  No package's tests were
   committed without a recorded red and green run.
4. **`~/.local/bin/{cmake,ctest}` shims** on this host shadow the real
   tools and launch an unrelated GUI app; all commands use absolute paths
   (/usr/bin/cmake, /usr/bin/ctest, /usr/bin/ninja) — recorded in BASELINE.md.
