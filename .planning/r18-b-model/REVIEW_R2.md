# R18-B Review Round 2 (fresh independent context — authored nothing, judged R1's fixes too)

Verdict: **PASS-WITH-NITS** (0 × P0/P1/P2, 3 × P3 + 1 note). All six probe
families falsification-tested:

1. R1 clamp lost-write — PROVEN SAFE: clamp is a no-op under every reachable
   call pattern (per-tile step ≤ tileSize+halo ≤ span; walks guarantee m_top
   ≥ rasterH − tileSize at finish); even in the hypothetical binding case:
   no lost rows, no double-write, no backward motion.
2. Restoration — VERIFIED byte-faithful (28/29 paths diff-empty vs f326d8ca9;
   CMakeLists differs only by master's own R16 blocks + the re-anchored
   r15-C block); dead-reference check clean (all restored dependencies
   pre-exist on master; enforceEoPreflight signature matches).
3. Cross-track — CLEAN: no caller's exception expectations change (fsync
   failures return bool / throw the same RSOperatorError shape); chunk seams
   safe for all existing consumers.
4. Tests/CI — SAFE: no hang paths (W2 bounded by the worker's own 6 s sleep);
   ONNX fixture deterministic (sha256-identical regeneration), LE hosts only.
5. Claims audit — CT-1 git chain, W2 unreachability, W4b trace arithmetic all
   verified; ONE claim failure found (F1 below).
6. R1-missed sweep — slide memcpy proven overwrite-safe (forward loop,
   cleared ≥ 1); min{} overload correct; short-raster finish covered.

P3 findings and dispositions:
- F1 (evidence integrity): the R1 "span 24→16" fix silently no-op'd
  (unasserted replace) while two artifacts claimed it applied — fixed FOR
  REAL this time with an asserted edit (span = clamp(16+8,1,16) = 16).
- F2 (audit trail): .planning/r18-b-model/references/ (recovered r15-C docs)
  was gitignore-shadowed ('*' doesn't cross '/') — negation pair added, files
  committed with this change.
- F3 (asymmetric hardening): add()'s writes lacked the loud bounds guard the
  read side gained — CV_Assert added on the row offset (flush-then-add
  contract now loud on both sides).
- F4: W1/W2 wall-time note — already recorded in KNOWN_FINDINGS (R1-5/6).
