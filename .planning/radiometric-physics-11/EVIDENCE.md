# EVIDENCE — F14 radiometric-physics-11

Local evidence only; no online CI dependency. Each claim: command + exit code.

## Phase 0 — baseline audit (2026-09-15)

- `git fetch origin --prune` → exit 0 (first attempt hit a transient TLS EOF, retry clean).
- `git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`.
- `gh pr list --state open` → single open PR #1008 `zcode/radiometric-spectral-workbench`
  (45 files). Diff read for dedupe (read-only).
- `gh issue list --state open` → #1001..#1007, all other tracks' residuals; none radiometric.
- Master capability audit evidence: headers read (see BASELINE.md table);
  `grep -rin "declination|equation_of_time|earthSunDistance" src/` → only unrelated
  QgsMagneticModel; `grep -rln "viewZenith|view_zenith|BRDF|RossThick" src/` → empty.
- Worktree created: `git worktree add ../exp-rs-radiometric-physics-11 -b
  zcode/radiometric-physics-11 origin/master` → HEAD a5b11b7f10.
- `.gitignore`: appended one whitelist block for `.planning/radiometric-physics-11/*.md`
  (house pattern); `git check-ignore -v` verified non-ignored for tracked .md files.
- `cmake --preset dev-default` (worktree `build-dev`) → exit 0 (log: /tmp/configure_track.log
  summarized here at build time below).

## Build environment (fixed once)

- Host: 16 cores, 62 GiB RAM, load ~2.5 at start. Generator: Unix Makefiles (repo default for
  dev-default). Hard caps honored: `CMAKE_BUILD_PARALLEL_LEVEL=2`, build `-j2` (fallback `-j1`),
  tests `-j1`, `QT_QPA_PLATFORM=offscreen`. CPU/RSS/load sampled during builds (results below).

## Phase gates

(appended per phase as work completes)
