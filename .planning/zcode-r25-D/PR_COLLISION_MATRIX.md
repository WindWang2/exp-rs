# R25-D PR Collision Matrix (live; refreshed before push)

At track start (2026-10-28T17:03Z): 0 open PRs; remote has only merged r23/r24
leftover branches (ahead=0). Local sibling worktrees r25-A/B/C/E exist with
unpushed branches — no remote collision yet; their changed files are unknown
until they push.

Files this track touches that siblings may also touch (watch list):
- tests/CMakeLists.txt — APPEND-ONLY inside # BEGIN R25-D / # END R25-D; union rule under check_merge_registration_integrity.py.
- src/app/CMakeLists.txt — one line added (shell/operator_preflight.cpp) adjacent to task_panel_host.cpp; low conflict risk.
- src/app/shell/ribbon_controller.cpp, main_window_menus.cpp, main_window_docks.cpp, main_window_workbench.cpp, main_window.h, main_window_misc.cpp — shell files; if a sibling PR lands first, rebase and re-verify the union (no semantic overlap expected: their tracks are science/data/agent/teaching).
- src/app/workbench/{selection_context.*,command_defs.cpp} — ContextRules additions are append-only families.

Pre-push protocol: git fetch --prune; list open PRs; intersect changed files; merge or rebase accordingly; re-run affected tests.
