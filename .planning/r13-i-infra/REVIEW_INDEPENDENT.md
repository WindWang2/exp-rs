# R13-I Independent Review — REVIEW_INDEPENDENT.md

- Reviewer: independent subagent (no implementation involvement).
- Scope: `git diff origin/master...HEAD` (8 commits, 892295685e36..6a4ed9df92), worktree `exp-rs-r13-i-build-infra`.
- Method: line-by-line diff review + ledger cross-check + live fixture repros (python/bash only; no cmake builds; no file modifications outside this review file).
- Date: 2026-10-14.

## Verdict summary

**BLOCK** — 1×P1, 3×P2, 8×P3. The diff is genuinely high quality: every evidence claim I could cheaply reproduce reproduced (see "Verified claims" at the end). The blockers are small, enumerable fixes: the new census gate is wired nowhere, the track's own evidence/ledger is gitignored (contradicting the ledger's "committed" claim), and the clang-tidy header filter is fragile in symlinked checkouts. None of the required fixes touches src/**.

---

## P1 — must fix before merge

### P1-1. The "registration census gate" is wired into nothing — it gates nothing
- **File:** `scripts/check_test_registration.py` (whole file); missing wiring in `tests/CMakeLists.txt` / `.github/workflows/ci.yml`.
- **Evidence/repro:** repo-wide grep for `check_test_registration` finds only the script itself. It is not a ctest test, not a CI step, not referenced by TEST_INFRA.md or any script. Contrast: the repo already has the exact precedent this needs — `test_model_library_generator_parity` at `tests/CMakeLists.txt:15637` runs a generator `--check` as a ctest lane. I ran the census directly: it works (`997 test_*.cpp; 5 pattern-ok; 0 orphan(s)`, exit 0 static and with `--build-dir build-r13-i`) — but nothing ever invokes it.
- **Impact:** the exact defect class it was created for (#1494: a test file present in the tree but registered nowhere, silently never compiled or run for months) can regress silently the day after this merges. The commit message `9420c559c9` ("register orphan challenger m1_2 harness + add registration census gate") and FINAL_REPORT ("FIX — … census gate") overstate the deliverable: what shipped is a manual diagnostic, not a gate.
- **Suggested fix:** register it as a ctest test (static mode is sufficient and portable; build mode optional via a configured tree), following the `test_model_library_generator_parity` pattern with the build-pinned Python interpreter; or add a CI step running `python3 scripts/check_test_registration.py`. One paragraph in TEST_INFRA.md documenting when it runs.

## P2 — fix before merge or explicitly defer with an owner

### P2-2. The entire track ledger + evidence is gitignored; ROUNDS.md's "Real data artifact committed" claim is false
- **File:** `.gitignore:142` (`.planning/*`, no `r13-i-infra` negation); claim at `.planning/r13-i-infra/ROUNDS.md` Round 4 ("Real data artifact committed: …master-runs-2026-10-14.json + master-status-2026-10-14.json").
- **Evidence/repro:** `git check-ignore -v .planning/r13-i-infra/ROUNDS.md` → matched by `.gitignore:142`; `git ls-files .planning/r13-i-infra/` → empty; `git status --porcelain` → clean (ignored). The repo convention for exactly this is one line below: ~60 other tracks add the three-line `!….planning/<track>/` + `…/*` + `…/*.md` negation triple.
- **Impact:** the GOAL.md exit gate demands "single PR with evidence matrix"; none of the ledger, the evidence JSON (including the only saved dump of the #1491 master-run history), or this review can ship in the PR. A reviewer checking the branch on GitHub sees zero evidence. The "committed" wording in ROUNDS.md is factually wrong and misled me for one check cycle.
- **Suggested fix:** add the standard `.planning/r13-i-infra/` negation triple to `.gitignore` (lead decision — the file sits outside this track's code allow-list, but OWNERSHIP.md already claims `.planning/r13-i-infra/**` as write scope, so the intent is unambiguous); fix the ROUNDS.md wording if the JSON stays deliberately local.

### P2-3. PATTERN_ALLOWLIST grants "pattern-ok" by filename alone — its stated justification is never verified
- **File:** `scripts/check_test_registration.py:34-39, 92-97`.
- **Evidence/repro:** fixture — temp `tests/` dir whose `CMakeLists.txt` contains no registration at all, plus one orphan `test_verifier_core_14.cpp`:
  ```
  census: 1 test_*.cpp; 1 pattern-ok; 0 orphan(s)
    pattern-ok: test_verifier_core_14.cpp — registered via SICNU_VERIFIER_LANES foreach
  exit: 0   <- an UNREGISTERED orphan passed as pattern-ok
  ```
  The entry's reason string asserts registration "via SICNU_VERIFIER_LANES foreach (tests/CMakeLists.txt)" but the script never checks that the foreach (or even the file) still exists. If the foreach is renamed or deleted later, all allowlisted files become orphans and the census still passes green. (The anchoring itself is precise: `test_verifier_core_15.cpp`, `test_verifier_core_14a.cpp`, `xtest_verifier_core_14.cpp` are all orphan-visible — verified by regex probe. Also note only 5 of the 7 lanes' files exist; `core`/`drift` entries are latent, which is fine.)
- **Impact:** the gate has a demonstrated hole exactly in the family it was written to handle; combined with P1-1 (unwired), the protection is nominal. Severity held at P2 rather than P1 because the realistic trigger (foreach removal) is a deliberate edit, and only exact-name files are masked.
- **Suggested fix:** make each allowlist entry `(regex, required_cmake_marker, reason)` and require the marker text (e.g. `SICNU_VERIFIER_LANES`) to be present in the scanned CMake text for pattern-ok to apply; otherwise the file is an orphan.

### P2-4. clang-tidy header filter uses the logical checkout path and an incomplete regex escape — symlinked checkouts can silently suppress first-party header findings
- **File:** `scripts/run_clang_tidy_changed.sh:56-57`.
- **Evidence/repro:** `REPO_ROOT="$(pwd)"` (logical path; the script never `pwd -P`/`realpath`), and the sed escape class `s/[][\\.^$*]/\\&/g` omits regex metacharacters `+ ? ( ) | { }` — a legal directory name containing any of these yields an invalid `--header-filter` regex (clang-tidy then errors, which is fail-closed but confusing). The symlink concern: the filter is `^<REPO_ROOT>/src/`; clang/clang-tidy header diagnostic paths follow the path spelling used during include resolution, and in a symlinked checkout (very common for local worktrees, e.g. `~/projects` → elsewhere) the spelling can be the realpath while `REPO_ROOT` is the logical path — the anchored filter then matches nothing and every first-party header diagnostic is suppressed, while the gate still looks green. The ledger's Round-16 proof (injected header bug reported at the header, exit 123) ran on a non-symlinked worktree, so it does not cover this case.
- **Impact:** under-enforcement of a now-blocking gate precisely on the header-diagnostic path that #1495 made blocking. Not P1 because CI's runtime (`$GITHUB_WORKSPACE` on `ubuntu-latest`, fresh non-symlinked clone — verified the expression and step wiring) is unaffected; the exposure is local/adversarial-reviewer checkouts.
- **Suggested fix:** `REPO_ROOT="$(realpath "$(pwd)")"` (and match against realpath'd compile db paths is unnecessary — clang-tidy anchors on diagnostic paths; realpath'ing the root closes the logical/real mismatch for the common case), and extend the sed class to `+?()|{}` or build the filter with a quoted-ERE helper.

## P3 — defer-worthy nits

### P3-5. Census static mode matches stems inside CMake comments
- **File:** `scripts/check_test_registration.py:88-90`.
- **Repro:** fixture `CMakeLists.txt` containing only `# qt_add_executable(test_real_thing test_real_thing.cpp)` + orphan `test_real_thing.cpp` → census exit 0. A commented-out registration masks an orphan in static mode. Build mode (`--build-dir`, compile_commands ground truth) catches it, and the repo only has 997 well-formed files today, so this is a static-mode-only hole.
- **Fix:** strip `#`-to-EOL comments before the stem search, or require the match to be on a line containing a command keyword (`add_executable|sicnu_add_test|qt_add_executable|target_sources`). Document that static mode is heuristic and build mode is authoritative.

### P3-6. verification_ladder: eager argparse default — garbage env aborts even `--list`; explicit flag can still outrank an env pin
- **File:** `scripts/verification_ladder.py:398`.
- **Repro:** `_default_build_jobs()` matrix verified live: unpinned→8, `2`→2, `abc`→SystemExit (rc 1), `-3`→1, `0`→1. But `CMAKE_BUILD_PARALLEL_LEVEL=garbage verification_ladder.py --list` also aborts (the default is evaluated at parser construction), and `--build-jobs 8` silently amplifies past an env pin of 2 (env is a *default*, not a cap — the docstring says "never be silently amplified by this script", which is only true for the default path).
- **Impact:** cosmetic/edge; fail direction is safe everywhere.
- **Fix:** compute the default lazily (default=None, resolve after parse), and print a stderr note when an explicit `--build-jobs` disagrees with a set `CMAKE_BUILD_PARALLEL_LEVEL`. Also consider rejecting negatives like non-integers instead of clamping, for consistency.

### P3-7. check_mcp_stdio preflight breaks bare-name (PATH-resolved) invocation
- **File:** `scripts/check_mcp_stdio.py:26`.
- **Repro:** missing exe → clean `MCP STDIO CHECK FAIL … (not found or not executable)`, rc 1 (verified). Relative `./mcp.exe` from the exe's cwd passes the preflight (verified). But a bare name that is on `PATH` yet not in the caller's cwd now fails the preflight even though `Popen` (env carries `PATH`) would have resolved it pre-fix. The default `--exe build-dev/sicnu_geo_rs` is path-shaped and there are no repo callers, so nothing is broken today.
- **Fix:** when `isfile` fails, fall back to `shutil.which(args.exe)` before failing closed.

### P3-8. ci_master_status classification edges
- **File:** `scripts/ci_master_status.py:31-42, 67-88, 158`.
- **Verified good (live fixtures):** "Compile Core & Test Targets" → failed-build (build token precedes test token, as documented); "Post ccache / save" / "Install Qt 6.8" / "actions/checkout" → infra; multi-job run label picks failed-test over failed-configure per `_STAGE_PRIORITY`; malformed runs (non-array, missing `databaseId`) → exit 2 fail-closed; committed evidence artifact reproduces exactly (25 runs, counts cancelled 15 / failed-unknown 8 without jobs-dir vs recorded 3 failed-test + 5 failed-unknown with jobs-dir — consistent); gate scenarios rc 0/1/1/1 as ledger claims.
- **Edges:** (a) a job with conclusion `timed_out` but no failed step classifies as "infra" (the code comment claims "runner died" — a timeout is not infra, and the tool has a dedicated `timeout` run-level class it never applies at job level); (b) all tokens are substrings — a contrived step "Attest provenance" would classify failed-test via the "test" token; (c) `createdAt` is compared as raw strings — correct for gh's `Z`-suffixed RFC3339, but a hand-edited dump mixing `+02:00`-style offsets could mis-order "latest completed". (d) `skipped`-class runs count as "completed" for latest-completed selection — fail-closed, fine, but worth a comment.
- **Fix:** treat job-level `timed_out` with no failed step as a distinct per-job class (or map to "timeout"); use word-boundary-ish token matching for the shortest ambiguous tokens; parse createdAt to datetime with the string as tiebreak/fallback.

### P3-9. build_with_otb.sh passes a non-integer env pin straight to make
- **File:** `scripts/build_with_otb.sh:35`.
- **Reasoning:** `CMAKE_BUILD_PARALLEL_LEVEL=abc` expands to `make -jabc` → make errors out (fail-closed, but noisy), while the ladder twin now rejects the same value with a typed `SystemExit` — the two tools disagree on the same governance variable. Empty/unset falls back to `nproc` correctly.
- **Fix:** one-line integer validation mirroring `verification_ladder._default_build_jobs()`.

### P3-10. clang-tidy lane fallback can silently pass on ancient git / exotic checkouts
- **File:** `scripts/run_clang_tidy_changed.sh:34-44`.
- **Repro (shape):** when neither `origin/master` nor `HEAD~1` resolves, `git diff` fails into an empty `FILES` and the script prints "nothing to lint", exit 0 — a silent pass. I verified the complementary fact that protects CI: on git 2.56 the ci.yml fetch step (`git fetch --no-tags --depth=1 origin master`) *does* leave `origin/master` resolvable (the well-known "command-line refspec doesn't update remote-tracking refs" gotcha does not apply to a bare branch name with modern git — tested with a synthetic remote where the tip moved). GitHub-hosted runners ship git ≥2.4x, so the CI path is safe; the exposure is local runners on old git.
- **Fix:** after falling back to `HEAD~1`, verify it too (`git rev-parse --verify --quiet`), and if both are missing exit 2 (fail-closed) instead of diffing an unresolvable ref.

### P3-11. Concurrency expression: benign same-SHA group sharing; one unverifiable stat
- **File:** `.github/workflows/ci.yml:17-20`.
- **Verified:** YAML parses; `==` binds tighter than `&&`/`||`, so the group is per-SHA for push, per-`github.ref` for PR; `cancel-in-progress` is true only for `pull_request`; `merge_group` is not in `on:` so the expression never sees that event; push to `master`/`main`/`agent/**` all get singleton groups with cancel=false. Semantics are correct.
- **Nits:** (a) pushing the *same* SHA to two branches (e.g. `agent/x` then fast-forward merge to `master`) makes both push runs share one group → the second queues behind the first rather than running concurrently (no cancellation, benign serialization); (b) the comment's "30 of the last 40 master runs were cancelled" is not reproducible from the committed 25-run dump (15/25 = 60% cancelled — consistent in spirit, but the saved evidence only covers 25).

### P3-12. perf_report: wall_ms disappearing from AFTER is not flagged as a signal
- **File:** `scripts/perf_report.py:70-80`.
- **Verified:** output for full records is byte-shape identical to pre-change (column math `%-5s ` reproduces the old spacing for wall/rssd; cpu has one fewer padding space but the same field layout via `%s`); missing metrics print `(no data)` without KeyError; the +40% wall regression still flags on a missing-metrics record (live fixture).
- **Nit:** when BEFORE has `wall_ms` and AFTER lacks it, the regression check is silently skipped — a metric vanishing between baselines is itself a signal; one extra warning line would make it honest.
- **Fix:** print `wall   (disappeared in AFTER)` and optionally include it in the regressions list.

---

## Verified claims (independent repro, all GREEN unless noted above)

- **collect_readiness.py bucket conservation** (live matrix over all 46 capabilities): all-pass→`ready`; one `cancelled`→`attention` with `cancelled:1`; one bogus status→`attention` with `unknown_status:1`; all-skipped→`insufficient-evidence`; `sum(counts) == capability count` in every case. No repo consumer reads READINESS.json `counts` (grep: only docs), so the two new keys are backward-compatible.
- **build.sh selftest** (hermetic, allowed): `bash scripts/build.sh selftest` → ALL PASS, exit 0 — the `$BASH` re-exec EACCES fix works on the real 100644 checkout. `$BASH` is always set under bash (script is bash-only: arrays + `BASH_SOURCE`), so the unset-`BASH` scenario dies earlier at `set -euo pipefail` under dash — no regression.
- **verification_ladder env-pin matrix** — see P3-6 (works as documented).
- **ci_master_status.py** — see P3-8 (classification, priority, gate, fail-closed all verified; committed artifact reproduces).
- **run_clang_tidy_changed.sh** — mock-clang-tidy fixture: filter constructed as `--header-filter=^<repo>/src/` (single argv); failing clang-tidy → xargs exit 123 → script exit 123 (blocks the step); empty diff → exit 0. `.clang-tidy` really has 12 checks and `HeaderFilterRegex: 'src/.*'` (the flooding source); root `CMakeLists.txt:52` sets `CMAKE_EXPORT_COMPILE_COMMANDS ON`, so the CI gate really has a compilation database. Only escaping concerns are P2-4/P3-10.
- **check_test_registration.py** — static + build mode exit 0 on the real tree; holes documented in P1-1/P2-3/P3-5.
- **perf_report.py** — see P3-12 (fix verified end-to-end).
- **check_mcp_stdio.py** — missing-exe clean fail-closed rc=1; relative-path usage intact.
- **tests/CMakeLists.txt hunk** — matches the family pattern exactly (qt_add_executable, Qt6::Core+sicnu_runtime, src+src/stubs+binary-dir includes, AUTOMOC, raw add_test per challenger-harness convention); target/test name appears exactly once (no duplicate); the harness has its own `main()` and no Catch2 dependency; TIMEOUT 300 is the deliberate sibling-None bound recorded in DECISIONS; unlabeled → it runs in CI tier-1 `-LE "SLOW|LONG_RUN"` (it passed 0.31s on the builder host per ROUNDS.md Round 13 — plausible and consistent with `ctest -N` Test #971).
- **Ownership** — `git diff --stat` shows exactly 13 files, all inside the allow-list (ci.yml, TEST_INFRA.md, packaging/appimage_toolchain.sh comment, tests/CMakeLists.txt, scripts/*). **No src/** or out-of-scope file touched.** Commit messages match content (verified `5bcebc7d65`'s "no unit test references fetch_tool" claim by grep over tests/). `git diff --check` clean.
- **ci.yml concurrency** — see P3-11 (expression semantics correct; merge_group not enabled; pyyaml parses).

## What I could not verify (stated plainly)

- No cmake builds run (per rules); the m1_2 harness compile/run evidence is taken from the ledger (Round 13) and the file's obvious self-containedness, not re-proven here.
- Windows behavior of every changed script is static-only (no host) — unchanged from the track's own honest NOT-EXECUTED list.
- The clang-tidy header-filter behavior in a symlinked checkout is reasoned, not reproduced (needs a configured build tree through a symlink; the concern and its fix are cheap either way).

---

## Round 2 re-review (2026-10-14, commits 58571bbe9b + da7bf1cd52 + worktree .gitignore)

**Final verdict: APPROVE.** All round-1 P1/P2 findings are closed with live repros; P3-5..P3-12 are fixed. One new P3 regression was introduced by a fix itself (word-boundary token over-correction); four small P3 nits remain. Nothing warrants a further round.

### Per-finding verdicts

| # | Round-1 finding | Verdict | Evidence |
|---|---|---|---|
| P1-1 | Census gate wired into nothing | **CLOSED** | `tests/CMakeLists.txt:15643-15648`: `test_registration_census` added inside the same `if(Python_EXECUTABLE)` block as the parity lane (15634), absolute paths throughout (`${Python_EXECUTABLE}`, `${CMAKE_SOURCE_DIR}/…`) so the lane is cwd-independent; `sicnu_set_test_env` applied (defined `cmake/SicnuTestEnv.cmake:277`, same as its neighbor); unique test name (grep: only the new block); TIMEOUT 60; documented in TEST_INFRA.md. Runs wherever ctest runs with tests enabled (Linux tier1/tier2). |
| P2-2 | Ledger/evidence gitignored; "committed" claim false | **CLOSED** | `.gitignore` gains `!.planning/r13-i-infra/`; `git status` now shows `?? .planning/r13-i-infra/` (visible, no longer ignored). Note: single-line negation makes *all* files under the dir committable (other tracks restrict to `*.md`) — correct here because the JSON evidence must ship. Change verified in the working tree (`git diff .gitignore`, one line). ROUNDS.md wording still says "committed" — now it will be true once staged. |
| P2-3 | Allowlist masking | **CLOSED** | `template_registrations()` expands the foreach family from live CMake text and requires both the `set(SICNU_VERIFIER_LANES …)` block and the `test_verifier_${LANE}_14` template string. Fixture repros: orphan `test_verifier_core_14.cpp` with no anchor → exit 1; anchor present but lane dropped from the set → exit 1; full anchor → exit 0. Real tree: 6 template-ok, 0 orphans (the 7th file, `drift`, is registered literally — consistent with its extra block at :15688). |
| P2-4 | Tidy filter logical path + incomplete escape | **CLOSED** | Verbatim snippet sourced from the script and run in a symlinked directory produces `^(/tmp/tidyln/link|/tmp/tidyln/realroot)/src/` (correct dual anchor); nasty path `/tmp/nasty+dir(p)\|x` produces `^/tmp/nasty\+dir\(p\)\|x/src/` (every ERE metachar escaped, letters untouched). In-repo run still single-anchor `^<repo>/src/` (git resolves the symlink itself, so logical==physical — the alternation is belt-and-suspenders for other path spellings). Fail-closed ref check verified: shallow clone + junk base → "refusing to silently pass", exit 2. Mock clang-tidy exit 1 → script exit 123. |
| P3-5 | Comment-masking in census static mode | **CLOSED** (residual nits → N3) | `uncommented()` strips `#`-to-EOL; fixture: commented-out registration no longer masks an orphan → exit 1. |
| P3-6 | Ladder eager default | **CLOSED** | `--list` with `CMAKE_BUILD_PARALLEL_LEVEL=garbage` → exit 0 (no resolution on list); run mode rejects `-3` and `abc` with typed SystemExit before the build-dir check; `--build-jobs 8` against pin 2 prints `note: --build-jobs 8 overrides pinned CMAKE_BUILD_PARALLEL_LEVEL=2` on stderr; equal flag/pin prints nothing. |
| P3-7 | MCP preflight breaks PATH invocation | **CLOSED** | Bare name on PATH but not in cwd now resolves via `shutil.which` against the child PATH and fails at the protocol line (`MCP STDIO CHECK FAIL <resolved>`), not the preflight line; genuinely missing exe still prints the preflight verdict, exit 1. |
| P3-8 | ci_master_status edges | **PARTIALLY CLOSED — new regression (N1)** | Fixed and verified: job-level `timed_out` with no failed step → `timeout`; `Attest provenance` → failed-unknown (no longer failed-test); mixed-offset `createdAt` dump sorts by UTC instant (`+02:00` vs `Z` fixture resolved correctly); committed 25-run evidence reproduces unchanged (15 cancelled / 8 failed-unknown / 2 pending, gate FAIL). **Regression:** see N1. |
| P3-9 | build_with_otb non-integer pin | **CLOSED** (nit → N4) | Case: empty → `nproc`; `abc`/`0`/`-3` → typed exit 2; `2` → `make -j2`. Verified by inspection (cannot run make). |
| P3-10 | Tidy lane unresolvable-ref silent pass | **CLOSED** | Live: depth-1 clone + junk base ref → exit 2 with explicit refusal message. |
| P3-11 | Concurrency comment stat | **CLOSED** | Comment now cites `.planning/r13-i-infra/evidence/master-runs-2026-10-14.json` (15 cancelled / 0 success in the saved 25-run dump — evidence-backed); ci.yml still parses (pyyaml); expression untouched and was already correct. |
| P3-12 | perf_report metric disappearance | **CLOSED** | Fixture (BEFORE has cpu/rss, AFTER lacks them): prints `cpu (disappeared in AFTER)` / `rssd (disappeared in AFTER)`; wall regression math unchanged and still flags +40%. |

### New findings from the fixes (all P3)

- **N1 (P3, regression from the P3-8 fix): `_TEST_RE = \b(?:ctest|test|execute)\b` breaks the plural.** `scripts/ci_master_status.py:57`. Fixture: a failed step named "Run unit tests" — one of the most common step names in GitHub workflows — now classifies `failed-unknown` (round 1 classified `failed-test`). Impact is label-only and fail-closed (the run still counts as a failure; the gate still fails), but it degrades the classifier's core purpose for a mainstream naming pattern. Fix: `\btests?\b` ("attests" stays safe — no word boundary before "tests" mid-word).
- **N2 (P3, doc drift): the new census comment in `tests/CMakeLists.txt:15641-15642` still says "PATTERN_ALLOWLIST entry whose anchor is itself verified"** — the script replaced `PATTERN_ALLOWLIST` with `template_registrations()` in the same commit. Fix: one-line comment reword.
- **N3 (P3, residual static-mode vectors, both fail-direction-safe):** `uncommented()` (a) does not handle CMake bracket comments — a stem inside `#[[ … ]]` continuation lines still counts as registered (masking vector; **inert today**: `tests/CMakeLists.txt` contains zero `#[[` occurrences) and (b) truncates at `#` inside quoted strings, which can produce a false orphan (noisy, fail-closed). Build mode (`--build-dir`) is unaffected and authoritative. Acceptable as documented heuristic; optional hardening.
- **N4 (P3, trivial): `build_with_otb.sh:37` case accepts `"00"`** (falls to the catch-all, then `make -j00` errors out — fail-closed but ungoverned), while `build.sh`'s convention rejects `0*`. Optional: extend the pattern to `*[!0-9]* | 0*`.

### Round-2 hygiene checks

- `git diff --check` clean over both the committed range (`origin/master...HEAD`, now 10 commits) and the working tree (`.gitignore`).
- Ownership: the two new commits touch only `tests/CMakeLists.txt`, `TEST_INFRA.md`, `scripts/*`, `.github/workflows/ci.yml` (comment only); worktree adds `.gitignore` (one line). No `src/**` or out-of-scope files.
- Commit messages match content (census wiring + template anchors + comment stripping in 58571bbe9b; all named P2/P3 fixes present in da7bf1cd52 — verified line-by-line against this review's suggestions).
- Re-verified green from round 1 and unbroken: census real tree 0 orphans; ladder run-mode matrices; mcp preflight; perf_report full-record output shape; tidy filter single-anchor construction; collect_readiness untouched.
