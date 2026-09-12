# ISSUE TRIAGE — re-verified on `origin/master` @ `f316dfdbb4` (2026-09-12)

All historical cartography-related leads are re-located on the NEW master,
not fixed from old line numbers. 0 issues are open at baseline.

## The five goal-prompt leads

| Issue | State | Disposition | Evidence on new master |
|-------|-------|-------------|------------------------|
| #864 [Cartography/Solver] relaxation oscillation between clampSizes and match_width/match_height causes false non-convergence | CLOSED 2026-09-11 | fixed-by-later-merge (base commit `f316dfdbb4`) | `composition.cpp`: `relaxHard()` now calls `restoreRects(mPreSolveRects)` when the pass budget is exhausted (layout rolls back instead of parking mid-oscillation), and `computeTargets` for `match_width/match_height` clamps the leader-derived size to the follower's `min_size_mm`/`max_size_mm` (removes the clampSizes↔match fight at the root). |
| #865 [Cartography/Solver] fit_content permanently blocked on zero-size initial rect | CLOSED 2026-09-11 | fixed-by-later-merge (same commit) | `composition.cpp` `fit_content` branch: when `toRect` rejects a zero-size rect, the leader falls back to the declared `rect_mm` values (x,y kept; w/h 0 allowed) instead of returning `Blocked` forever. |
| #866 [MapSpec/Condition] operandValue omits Op::Has → `has(x) == false` crashes with unknown-path | CLOSED 2026-09-11 | fixed-by-later-merge | `mapspec_conditions.cpp` `operandValue()` now handles `ConditionAst::Op::Has` returning the boolean of `resolvePath(...) != nullptr`. |
| #877 [MapSpec/Condition] IEEE754 NaN in compareValues corrupts comparison | CLOSED 2026-09-11 | fixed-by-later-merge | `mapspec_conditions.cpp` `compareValues()`: `isnan(a) || isnan(b) → return (op == "!=")` — NaN never satisfies an ordering, `!=` is the only true comparator. |
| #867 [Cartography/Harness] RecipeCatalog numeric bindings treated as false | CLOSED 2026-09-11 | fixed-by-later-merge; **ownership: Harness track** | Fix lives in `src/agent/harness/recipe_catalog.cpp` — outside this track's ownership. This track only guarantees the MapSpec-side contract stays compatible (condition evaluation does not depend on recipe bindings). |

M0 turns each of the four in-ownership fixes into named regression-corpus
entries (old code fails, new code passes) and extends the surrounding
semantics (ordering totality, has() null-value semantics, oscillation
detection, convergence trace).

## Adjacent closed issues consumed by this baseline

- #878 [Cartography/Style] rule-based renderer double rendering — fixed in
  `style_compiler.cpp` (container rules no longer get the default symbol).
- #805/#804/#802/#781 — earlier solver/condition semantics, fixed pre-8.0,
  covered by existing tests.
