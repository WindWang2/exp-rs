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

## OUT_OF_SCOPE (found during validation, 2026-09-13)

- `tests/test_capability_drift` — 2 pre-existing failures on master, both outside
  this track's ownership (verified by diff-scope: this track's src/agent changes are
  only the 115-pin updates; knowledge/recipe files untouched):
  1. "every registered spatial tool carries knowledge": `cartography:diff_templates`,
     `cartography:explain`, `cartography:export` have no knowledge entries in
     `data/agent/capabilities/tools.json` (cartography/agent-harness track).
  2. "deleted near-clone recipe ids resolve through aliases":
     `harness.optical_ndvi_landsat` alias resolution (agent harness recipe track).
- Both flagged for the owning tracks; not fixed here to avoid ownership takeover.

## OUT_OF_SCOPE (appended 2026-09-13, help coverage)

- `tests/test_help_coverage` — 2 pre-existing failures on master (this track's
  data/help diff is data_io.json only, +127 lines):
  1. Help composition errors: ALL 307 entries of `data/terms/rs_glossary.json`
     (#953 i18n track) lack the `id` field — composition reports
     "invalid id ''" per term.
  2. `command.workflow.new/open/save/run` shell commands lack knowledge
     entries (workflow/workbench track).
- Both flagged for owning tracks; not fixed here.

## Phase 6-7 validation (2026-09-13, post review-fix batch)

- Final incremental build: `ninja -j2 test_cn_products test_io_products
  test_io_product_registry test_satellite_products test_help_coverage
  test_product_import_dialog test_algorithm_meta_drift test_capability_knowledge
  test_capability_drift capability_knowledge_tool` → exit 0 (1282/1282 then
  1242/1242 and 110/110 steps across passes; errors 0).
- `QT_QPA_PLATFORM=offscreen ./tests/test_cn_products` →
  **All tests passed (1360 assertions in 28 test cases)** (after aligning the
  scale test with the bounded-enumeration contract: saturated 524-entry
  directories either import or refuse diagnosably — both asserted).
- `./tests/test_capability_knowledge` → All tests passed (1036 assertions,
  12 cases) after `capability_knowledge_tool gen-meta` (115 sidecars, 13
  shared-grid operators, relations ok) + `capability_enrichment.py`
  (applied 115/115) + `gen-pages` (index + io pages).
- `./tests/test_satellite_products` → All tests passed (479 assertions, 20 cases).
- `./tests/test_io_products` → All passed (38 assertions). `./test_io_product_registry`
  → All passed (46 assertions). `./test_algorithm_meta_drift` → All passed
  (2270 assertions). `./tests/test_product_import_dialog` → All passed (73 assertions).
- Known-inherited failures (out of scope, owning-track issues, see OUT_OF_SCOPE):
  test_capability_drift ×2 (cartography tool knowledge ×3 ids; recipe alias),
  test_help_coverage ×2 (rs_glossary 307 entries without `id`, #953;
  command.workflow.* knowledge, workflow track).
- Existence + wording assertions (goal template): both clean (empty output).

## Review-2 fix verification (2026-09-13)

- Rebuild (operators+deps) exit 0; rerun after F1/F2/F3/F7/F8/F9/F14c fixes:
  - test_cn_products → All tests passed (1363 assertions in 28 cases)
  - test_io_products → All passed (38); test_io_product_registry → All passed (46)
  - test_satellite_products → All passed (479); test_product_import_dialog → All passed (73)
- origin/master unchanged at 7d78059d1a (fetch re-checked at rebase time).
