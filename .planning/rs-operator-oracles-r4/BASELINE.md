# BASELINE.md — Track 7 (R4): RS Operator Correctness Deep Audit

Phase 0 baseline inventory. Measured 2026-09-27 on Linux (worktree
`/home/kevin/project/exp-rs-operator-oracles-r4`).

## 1. Measured git baseline

- `origin/master` after `git fetch origin`: **`15e5c66b5`** (Merge PR #1333,
  hardening/exp-capsule-debugger-study-r3). Identical to the SHA quoted in the
  task brief — master has **not** advanced since the brief was written.
- Local `master` == `origin/master` (0 ahead / 0 behind).
- Track branch: `hardening/r4-operator-oracles` cut from `origin/master`
  `15e5c66b5` in isolated worktree `../exp-rs-operator-oracles-r4`.
- Repo root (`/home/kevin/project/exp-rs`) treated as read-only except for
  the `git fetch` + `git worktree add` pair above (done).

## 2. Open PR inventory (gh pr list, 2026-09-27)

| PR | branch | title (abridged) | overlaps this track? |
|---|---|---|---|
| #1334 | fix/review-p1-security | default-deny MCP/CLI sandbox… | `src/operators/framework/model_catalog.{h,cpp}` (we do NOT touch these), `tests/CMakeLists.txt` (**overlap — rebase before merge**), other files outside scope |
| #1335 | fix/review-p0-build-restore | restore master build/CI (review P0) | `tests/CMakeLists.txt` (**overlap — rebase**), `tests/support/d15_e2e_pipeline.cpp` + test files outside scope |
| #1336 | hardening/closure-ui-runtime-r4 | i18n/help/lab-pack pins | tests only, no overlap with our files |
| #1337 | hardening/closure-workflow-contracts-r4 | workflow/agent contracts | tests only, no overlap with our files |
| #1338 | hardening/closure-io-processing-r4 | io/processing correctness gaps | `src/operators/io/io_operators.cpp`, `tests/test_raster_ndvi.cpp` — **no overlap with `src/operators/rs/`** |

File-overlap conclusion:
- **No open PR touches `src/operators/rs/`, `src/analysis/`, or
  `docs/verification/KNOWN_ANSWER_MATRIX.md`.** Green field for the audit.
- `tests/CMakeLists.txt` is contested by #1334/#1335. This track appends new
  test-target registrations only; PR description must state the rebase
  expectation.
- temporal family (rs:temporal_*) was deep-audited by the
  flash-temporal-phenology-12 track; the sweep below confirms temporal
  stream NoData handling is correct (temporal_stream.cpp normalizeAndMask),
  so this track does not modify it.

## 3. Count anchor table (Phase 0 re-measurement)

| anchor | brief value | measured | command |
|---|---|---|---|
| `src/operators/rs/*.cpp` | 128 | **128** | `find src/operators/rs -name '*.cpp' \| wc -l` |
| `src/operators/rs/*.h` | 131 | **131** | `find src/operators/rs -name '*.h' \| wc -l` |
| REGISTER_RS_OPERATOR | 157 | **158** (drift +1 vs brief) | `grep -c REGISTER_RS_OPERATOR src/operators/rs/rs_operators_init.cpp` |
| `Rs*Operator` class decls | 151 | **151** | grep class decls in headers, sort -u |
| `src/analysis` files | 111 | **111** | `find src/analysis -type f \| wc -l` |
| rs .cpp containing nodata/NoData | 85/128 | **85/128** (43 files without, confirmed) | `grep -liE nodata src/operators/rs/*.cpp \| wc -l` |
| known-answer carriers | 2 | **2** (`test_known_answer_corpus.cpp`, `_8.cpp`) | `ls tests/test_known*.cpp` |
| known-answer test targets registered | — | 194 `sicnu_add_test(test_*)` lines | grep tests/CMakeLists.txt |

## 4. Review materials consulted

- `docs/verification/KNOWN_ANSWER_MATRIX.md` (7.0/8.0 analytic-truth
  tradition) — exists on master; extended by this track with an R4 section.
- Open PR bodies #1334–#1338 via `gh pr view` (file lists above). The
  "142 pre-existing failing tests" classification in #1335 concerns master
  CI restore, orthogonal to the new suites added here (all new tests are
  designed to pass on a good build; verdicts recorded in EVIDENCE.md).
- `docs/PARALLEL_TRACKS_10.md`, PROJECT_REVIEW_DOSSIER_5.0.md,
  AUDIT_DOSSIER_ISSUES_747_760.md, PR_TRIAGE_REPORT_2026-09-16.md — present
  at repo root / docs per brief.
- Open issues: **0** (`gh issue list --state open` returned none).

## 5. Track boundary declaration (whitelist)

Allowed:
- `src/operators/rs/` (minimal defect fixes)
- `src/operators/framework/` (only if a base-class-level convention is
  unavoidable; every touch ledgered) — **target: zero touches**
- `src/analysis/` (minimal defect fixes in numeric cores called by operators)
- `src/processing/algorithms/` — see DECISION note below
- `tests/` + `tests/CMakeLists.txt`
- `docs/verification/KNOWN_ANSWER_MATRIX.md`
- `.planning/rs-operator-oracles-r4/`

DECISION (recorded in DECISIONS.md): several audited defects live not in the
operator file but in the kernel it calls, under `src/processing/algorithms/`
(e.g. PCA mean/covariance ingesting sentinel pixels; spectral derivative
kernel). The brief's whitelist names `src/analysis/` as "the numeric core
called by operators"; in this codebase the rs numeric core actually lives in
`src/processing/algorithms/` (verified: rs operators include
`processing/algorithms/...`). Fixing the *kernel* rather than papering over
it in the operator is the minimal, honest repair, so
`src/processing/algorithms/` is treated as the whitelisted numeric core,
with per-file justification in the ledger and matrix rows. Any file outside
these dirs → refused and ledgered.

Not allowed: everything else (GUI, plugins, workflow, io, runtime, docs
beyond the matrix, new operators/features/experiments).

## 6. Build & test environment (measured)

- cmake `/home/kevin/toolchain/cmake-dist/bin/cmake` (3.30.5) — note: CMake's
  find_program cannot locate ninja on this box's PATH; `-DCMAKE_MAKE_PROGRAM`
  must be passed explicitly.
- ninja `/home/kevin/toolchain/ninja` (1.12.1), invoked with explicit `-j2`.
- Configure: `-G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
  -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`
  (matches the proven `build-review` cache: GDAL 3.13.3 from pwb-sdks,
  Qt 6.11.2 system, PROJ 9.8.1, GEOS 3.15.0, OpenCV 5.0.0).
- Build dir: `build-r4/` inside the worktree (fresh).
- `QT_QPA_PLATFORM=offscreen` for any GUI-touching test; CTEST_PARALLEL_LEVEL=1.
