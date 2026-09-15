# REVIEW_LOG — qgis-editing-annotation-11

Review policy: main-agent full-diff review first (`origin/master...HEAD`), then one independent read-only adversarial review (subagent #2). All findings dispositioned here. P0/P1 must reach zero with evidence before PR.

## Round 0 — main-agent full diff review (2026-09-16)

Findings fixed before the independent review:
- M1 (P1): brush/erase strokes dereferenced the QPointer target layer in
  canvasMoveEvent/finishStroke — a layer deleted mid-stroke would crash.
  Fixed with explicit null-guards that cancel the stroke. (commit
  "fix(editing): guard brush/erase strokes ...")
- M2 (P3): trailing whitespace in the workbench mount — fixed (keeps
  `git diff --check` clean).

## Round 1 — independent adversarial review (subagent #2, 2026-09-16)

Scope: full diff `origin/master...HEAD` (52 files), all checklist areas.
Verdict at review time: **P0=0 P1=2 P2=4 P3=8**. Dispositions:

| # | Sev | Finding | Disposition |
| --- | --- | --- | --- |
| R1 | P1 | one-pass stddev (sumSq−mean²) catastrophically cancels on DN-like grids | **FIXED**: two-pass central moments in `rs_roi_semantics.cpp` (comment documents why); shift-invariance pinned by the a-priori grid oracle (stddev invariant to the +1000 band offset, exact within 1e-6). Honest note: a unit test that *discriminates* the unstable form needs O(1e6)+ magnitudes×pixels — infeasible in-suite; fix verified by construction + review. |
| R2 | P1 | atomic overwrite was remove-then-rename (crash window) | **FIXED**: rename-first (POSIX replace(2) is atomic), remove+rename only as fallback; docs qualified ("atomic on POSIX"). |
| R3 | P2 | GPKG test could silently pass via else-branch | **FIXED**: `REQUIRE(result.ok)` — driver present in this profile; regressions now fail loudly. |
| R4 | P2 | negative persistence test did not exercise writer-side failure | **FIXED**: header reworded to claim only what it tests (pre-IO gate); NEW test injects a genuine writer failure via a read-only directory (ErrCreateDataSource path: fail-closed, no temp residue). |
| R5 | P2 | mounted session inert (nothing attaches layers in-app) | **DISPOSITIONED (documented)**: docs §8 Known limits now states the mount starts empty and attach-wiring of MapToolManager start/stop is a follow-up. In-app wiring would cross into main_window/MapToolManager territory beyond minimal integration. |
| R6 | P2 | georef include comment factually wrong ("constructs") | **FIXED**: comment corrected to "dereferences the window returned by openGeorefDualWindow()" — the include IS required: compiling the pristine master TU fails with incomplete-type errors at the addDockWidget/member calls (evidence /tmp/mww_orig_err.log). |
| R7 | P3 | detach of a clean-but-editing layer stayed editing, contra header | **FIXED**: rollBack whenever editable and rollbackDirty (header contract restored). |
| R8 | P3 | validPixelCount "(band 1 mask)" doc drift | **FIXED**: header now documents running-min (band-intersection) semantics. |
| R9 | P3 | rubber band unbounded despite stamp cap | **FIXED**: rubber additions capped at the same bound (brush counter + erase mStampCount gate); erase drop-oldest wording clarified. |
| R10 | P3 | brush single-part refusal + cap path untested | **FIXED (refusal)**: new test "brush refuses a single-part target layer explicitly" (multipart stroke via two disjoint discs, refusal reason asserted). Cap/coalescing path remains covered by inspection only (2048-event loop impractical in-suite) — noted in docs. |
| R11 | P3 | commitFinished emission inconsistency | **FIXED (doc)**: signal contract documented in header (emitted after an attempt; pre-flight refusals return false without emitting). |
| R12 | P3 | second-window registration refused (first wins) | **DISPOSITIONED (note)**: matches the WorkbenchContextTool precedent on master; single-window desktop app. |
| R13 | P3 | predictable temp name | **FIXED**: temp name now `tmp-<pid>-<epochMSecs>`. Symlink-in-shared-dir risk accepted (desktop app, user-owned dirs) and documented. |
| R14 | P3 | residue: column-0 `);`, leftover probe block, dead helper | **FIXED**: all three removed; `git diff --check` clean. |

## Disposition summary

P0 = 0 (none found). P1 = 2/2 fixed. P2 = 3 fixed, 1 documented-as-follow-up (R5). P3 = 6 fixed,
2 dispositioned with rationale (R12 note, R10 cap-path inspection-only). All P1/P2 fixes re-verified:
`ctest -R "test_edit" -j1` → 10/10 after the fix batch (plus the final double-run in Phase 8).
