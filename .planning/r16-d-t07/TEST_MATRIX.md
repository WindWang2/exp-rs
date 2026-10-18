# R16-D-T07 Test Matrix

Build dir: build-r16-d (private, Ninja Debug, same configure recipe as r16-d BASELINE.md). Authoritative runs execute the built binaries directly.

## Families (GREEN×2 identical, exit 0)
| Executable | Cases | Assertions |
|---|---|---|
| test_r14_D_vector_edit_honesty | 10 (2 new R16-D cases) | 144 |
| test_qgis_display_manager | 25 (3 new R16-D cases) | 6489 |
| test_display_stretch | 12 | 41 |
| test_magic_wand_roi | 3 | 24 |
| test_workbench_host | 8 | 50 |

Baseline on clean master (pre-change build): same families 11/11 ctest-green (subset registered).

## New oracles
| WP | Test | Assertion core |
|----|------|-----------------|
| 1 | honesty "Edit lease is held for the session, exclusive, and committed" | lease HELD after toggle start (RED: released); 2nd acquire refused; saveEdits advances AssetRevision (RED: no_edit_lease) and session re-acquires on the relocated replacement (re-selected as canvas current); stop releases. |
| 1 | display "Relocation defers while the layer holds an edit session" | assetChanged mid-session: layer alive, editable+modified intact (RED: destroyed, then SIGSEGV); after session end the next change relocates (QPointer null, replacement id differs, DisplayLayerId identity kept). |
| 2/3 | honesty "Digitizing tools gate on geometry type and expose reasons" | non-vector current → disabled + reason in property & tooltip; point/line/polygon matrices (rings polygon-only etc.); enabled state clears the reason. |
| 5 | display "setLayerVisible fails typed when the tree node is missing" | happy path toggles + succeeds; missing node → failure `display.layer_tree_node_missing` (RED: success). |
| 7 | display "PiecewiseLinearEnhancement bisection matches the linear-scan reference" | 6109-value equivalence sweep incl. exact control points/midpoints (refactor guard). |

## Environment notes
- ctest-level strays in this minimal build dir (NOT_BUILT placeholders for targets outside the five families) are expected; direct-binary runs are the family verdict.
- The RED phase used `git stash` of production edits to build tests against pristine master — the exact opposite composition of the GREEN builds; no binary reuse across phases.
