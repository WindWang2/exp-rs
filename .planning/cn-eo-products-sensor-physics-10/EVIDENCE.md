# EVIDENCE — cn-eo-products-sensor-physics-10

## Phase 0 (2026-09-13)

- `git fetch --all --prune && git checkout master && git pull --ff-only` → exit 0,
  master = origin/master = 7d78059d1a6d316d606656759a506d17bc5e3b55.
- `git worktree add ../exp-rs-cn-eo-products-sensor-physics-10 -b zcode/cn-eo-products-sensor-physics-10 origin/master` → exit 0.
- `git check-ignore -v .planning/<track>/GOAL.md` → matched by local
  `.git/info/exclude` `.planning/` (machine-local; same as master commit 4c9a22ab0c);
  tracked `.gitignore` whitelist added this commit; planning files force-added.
- `gh pr list --state all --limit 40` → dedupe table in BASELINE.md.
- Audit evidence: file:line citations in CAPABILITY_MATRIX.md.

## Phase 1-4 (2026-09-13, implementation batch 1)

- Configure: `cmake --preset dev-default -G Ninja -DSICNU_LAB_SKIP_PYTHON_BINDINGS=ON`
  (TMPDIR redirected to /home/kevin/tmp — /tmp is a 32G tmpfs shared with
  concurrent tracks and hit transient ENOSPC during compiler-ABI probes).
  Exit 0, build-dev/build.ninja generated.
- Inherited gap found on master: `data/processing/algorithm_meta/capability/`
  has 111 sidecars while 114 rs: operators are registered (PR #956 added
  gaofen/zy3/hj imports without sidecars) → tests/test_capability_knowledge.cpp
  "D8 coverage 111/111" FAILS on master. This track fixes it as part of the
  capability integration work package (adds 4 sidecars incl.
  rs:cn_product_import, updates pins to 115).
- Registry data migration verified: old band_roles values (range midpoints,
  roles, role_reasons) byte-equivalent for gf1/gf2/gf6/zy3/hj via python diff;
  one transcription error in the new gaofen.json (gf2_pms B2-B4) caught by the
  diff and fixed before commit.

## Budget metering (phases 1-6 implementation, 2026-09-13)

- Phase 0 (baseline+planning): ~45 tool calls, 15 files touched, ~40 min wall.
- Phases 1-5 (implementation batch): ~140 tool calls, 36 files touched, ~3 h wall
  (build waits dominated; concurrent 10.0 tracks kept host load 11-19 on 16 cores;
  builds ran -j2 throughout, tests serial).
- Build evidence: configure exit 0; build-dev targeted builds exit 0 (r3: 253 targets
  after P0-era fixes; r4 in flight for review-fix batch + capability_knowledge_tool).
- Disk: /home peak usage noted (56G → 38G free during full builds); /tmp is a shared
  32G tmpfs used by other tracks — redirected TMPDIR to /home/kevin/tmp.
