# REVIEW_R2 — 2026-10-27

Reviewer: independent R2 (second round, fully re-derived; R1's file read for context only — every conclusion below is my own evidence). Base: origin/master = ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea. Branch: agent/r23-e-platform-plugin-release-20261026T170017Z (5 commits, HEAD b22f7ddd). All destructive work in the throwaway clone /home/kevin/r2-scratch (seeded from /home/kevin/exp-rs-r23e-clone); the real worktree was never modified (verified clean at b22f7ddd after all batteries).

Verdict: **APPROVE-WITH-FIXES**

Whitelist-clean, append-only, all 7 ctest registrations green, no zombie/orphan processes anywhere, and the R1 fixes are genuinely closed (I re-derived every one, including new edge cases the fix-closure appendix did not cover). The one P2 is a residual blind spot inside the same invariant R1's F2 targeted: quoted `add_test(NAME "...")` names — 4 of which exist on master today — are invisible to both the subset and the duplicate checks, demonstrated by a silent exit 0 after deleting all four. The rest are P3 hygiene/robustness items.

## Findings (P0-P3, each with file:line + command evidence)

### N1 (P2) Quoted `add_test(NAME "...")` registrations are invisible to the subset AND duplicate checks — live on master today
- Where: `scripts/check_merge_registration_integrity.py:61` (`ADD_TEST_RE` captures `"name` — quotes included — or `"M2-Challenger:` up to the first space) and `:108-113` (`_valid_name` rejects any captured name starting with `"`), so the name never enters either set.
- Live exposure: master has exactly 4 quoted add_test registrations — `tests/CMakeLists.txt:13025` (`"test_challenger_m3_2_harness"`), `:13049` (`"M2-Challenger: Empirical stress harness"`), `:13074` (`"M2-Challenger-2: ..."`), `:13098` (`"M3-Challenger: ..."`).
- Counterexample (scratch, branch r2-quote-drop): deleted ONLY those 4 lines from a HEAD copy of tests/CMakeLists.txt, committed, ran the gate `--base origin/master --head HEAD --require-refs` → **exit 0, `lost: 0+0`**. The names are absent from the BASE set too (module probe of `registration_sets` shows no challenger names except the unquoted `test_challenger_m1_2_harness`), so any loss of those registrations is permanently undetectable; a pasted-twice quoted add_test is equally invisible to check 3.
- Blast-radius honesty: all 4 sit next to `qt_add_executable` twins, so whole-block loss IS caught via the target set; the miss is line-level add_test mangling and duplicate-paste of quoted names — the narrower sibling of R1's F2 (which was rated P2 and fixed). Fix is cheap: strip surrounding quotes from the capture and keep space-containing names as canonical (CMake's add_test NAME legally allows them).
### N2 (P3) The `:!*.md` exclusion is case-sensitive — `*.MD` is still marker-scanned
- Evidence: scratch probe `docs/dev/r2_upper.MD` with `<<<<<<< HEAD` → flagged under `--all` (`conflict marker: HEAD:docs/dev/r2_upper.MD:1`), while `docs/dev/deep/r2_deep.md` (lowercase, deeply nested) is correctly excluded. The on-disk twin is equally case-sensitive (`f.endswith(".md")`, scripts/...:254) so the modes agree, but a Windows-authored `README.MD` quoting a marker would false-positive the gate. No uppercase-`.MD` file is tracked today (`git ls-files | grep -iE '\.md$'` → none outside lowercase), so latent-only.
### N3 (P3) The Python test leaks 3 temp dirs per test method (15 per suite run), never cleaned
- Where: `tests/test_r23_e_python_worker_typed.py:205` (`tempfile.mkdtemp(prefix="r23e_markers_")`), `:206` (`r23e_plugin_`), `:109` (`DaemonHarness.tmp`, prefix `r23e_daemon_`) — none has an `addCleanup`/`rmtree`; `DaemonHarness.close()` (…:170-192) tears down processes/sockets but not the directory.
- Evidence: `/tmp/r23e_*` count went 165 → 180 across one 5-test suite run (each of 5 setUps = 3 dirs). Harmless to correctness, but it litters dev hosts and CI containers every ctest run.
### N4 (P3) Branch-aware duplicate counter: demonstrated false negatives in exotic shapes (latent — none exist in the repo today)
- Module probes of `_branch_aware_counts` (scripts/...:116-154):
  - root + if-branch same name → 1 (both register when the condition holds): `"add_test(NAME dup_t)\nif(A)\n add_test(NAME dup_t)\nendif()"` → 1.
  - directly under `if(A)` vs under `if(A){if(B)}` → 1 (both live when A∧B).
  - one-line `if(A) add_test(NAME dup_t) endif()` → the registration on the if-line is swallowed entirely (the `^if\s*\(` match consumes the line and `continue`s) AND the endif is never seen, permanently shifting the stack depth; a paste-twice straddling the one-liner is then missed.
  - unclosed `if`s at EOF leave residual depth (changes signatures) but never crash.
- Mitigation: `grep -nE '^\s*if\s*\(.*(add_test|add_executable|sicnu_add_|qt_add_executable).*endif' tests/CMakeLists.txt` → no one-line-if registrations exist on the branch; master/HEAD scans report 0 duplicates. The dominant #1715 class (flat sequential paste, sibling ifs) is correctly caught — see re-verification below.
### N5 (P3) Gate exit-code discipline: docstring says "2 = usage/io error", but a missing git binary exits 1 via traceback
- Evidence: `env PATH=<no-git> python3 <gate> --require-refs` → `FileNotFoundError: [Errno 2] No such file or directory: 'git'` traceback, **exit 1** — indistinguishable from a findings exit and contradicting the documented contract (scripts/...:38-39). argparse usage errors do exit 2 (verified: `--bogus-flag` → exit 2), so only the io-error leg is misdocumented. Effect is still fail-closed (nonzero); a `try/except OSError → exit 2` around the git calls would close it.
### N6 (P4 notes, no action required)
- Git-mode marker scan anchors `$` on the raw blob line, so `=======\r` (CRLF file) is missed while the disk-mode twin (`rstrip("\r\n")`, scripts/...:237) would catch it — mode divergence in CRLF trees only.
- Native probe records logical `pwd`; with a symlinked TMPDIR the comparison against `env.workDir.string()` could mismatch. Irrelevant on this Linux lane (test is `UNIX AND NOT APPLE`); `pwd -P` would be the belt-and-braces spelling.

No P0/P1. **Whitelist: PASS** — `git diff --name-status origin/master...HEAD` touches exactly the 5 allowed paths (4 adds + tests/CMakeLists.txt); no src/** change; tests/CMakeLists.txt numstat is 59/0 (pure tail append after `# --- end R22-04 registration ---`, no deleted/modified existing lines); `.planning/*` is gitignored (`.gitignore:140`) so review notes are untracked.

## R1-fix re-verification (independent outcomes per finding)

| R1 finding | My independent outcome |
|---|---|
| F1 (P1) `:(glob)!*.md` exclusion inert | **CLOSED.** Fix is `:!*.md` (scripts/...:208). Counterexamples in scratch: markers in `scripts/dev/r2_probe.md` (nested), `r2_root_probe.md` (root), `docs/dev/deep/r2_deep.md` (deep) — all EXCLUDED in default and `--all` modes; control `scripts/dev/r2_probe.txt` DETECTED in both (exit 1, precise stderr line). Raw cross-check `git grep -E <marker> HEAD -- ':!*.md'` returns only the .txt probe. Residual: case-sensitivity → my N2. |
| F2 (P2) add_test-only registrations unprotected | **CLOSED** for unquoted names. Master advance in scratch (added `add_test(NAME r2_future_script_test ...)` + targets) → gate on the review branch reports `registration lost: add_test 'r2_future_script_test'` (+ both targets), exit 1; `--allow-removed r2_future_script_test,r2_future_target` silences both kinds by bare name. Base/HEAD counts now show the add_test axis (43→45, added 2+2). Residual: quoted names → my N1. |
| F3 (P2) unresolvable refs degrade to a passing gate | **CLOSED.** `--base no-such-ref --require-refs` → exit 1 with `failing (--require-refs)`; broken `--head refs/heads/nope --require-refs` → exit 1; without the flag on the clean worktree → exit 0 + explicit stderr degrade note (static mode). Wiring verified twice: source registration `tests/CMakeLists.txt:16801` and the built `build-E/tests/CTestTestfile.cmake:1128` both pass `--require-refs`. Corrupt/empty `.git` dir + `--require-refs` → exit 1 (fail-closed), without flag → static mode. |
| F4 (P2) waitForProbe half-written-file race | **CLOSED.** `tests/test_r23_e_native_terminal.cpp:92-99`: the poll only returns when the third line (cwd) is non-empty, otherwise keeps polling (25 ms steps, 5 s bound); an incomplete file no longer returns `valid=false`. Plus 10/10 consecutive green runs of the binary here (R1 had 15). |
| F5 (P3) path junk names / `#[==[ ]==]` comments | **CLOSED.** HEAD target set has 0 names containing `/`, `$`, `"` or `!` (module probe over the real worktree, 1039 targets); `#[==[ add_executable(...) ]==]` filtered, `#`-comment filtered, `sicnu_add_header_probes(<newline>runtime/... .h)` path argument skipped. |
| F5b branch-aware duplicate counting | **CLOSED on R1's cases and on my four new edges.** R1's: if/else siblings → 1; sequential paste → 2; sibling ifs → 2. Mine: if/elseif/elseif chain → 1 (correct: exclusive branches); nested if inside if + else → 1 (no false dup); duplicate INSIDE one branch → 2 (correctly flagged); unbalanced text (extra endif with no open if; unclosed ifs at EOF; heavy mixture) → no exception, extra-endif ignored safely (`len(branch) > 1` guard), counting still sane. The real master alternative-registrations `classroom_batch_conformance` / `launcher_parity_conformance` count 1 (no false positive on the actual 17k-line file). Residual exotic false negatives → my N4. |
| F6 (P3) git failures fail open | **CLOSED in code, one leg demonstrated live.** `scan_markers_git` rc>=2, `git show` rc!=0 and `ls-tree` failure all append explicit problems → exit 1 (scripts/...:192-200, 227-229, 273-278). Demonstrated: fake corrupt `.git` + `--require-refs` → exit 1. Like R1, I could not trigger a genuine mid-run git grep/show failure (rc≥2) — code-read only for those two legs. |
| F7 (P3) static vs git duplicate surfaces differ | **CLOSED.** `duplicate_counts_git("HEAD")` vs `duplicate_counts_disk` on the real worktree produce identical key sets (module probe; `tracked_cmake_files` now includes `*.cmake`). |
| F8 (P3) ctest-skip visibility | Documentation-only acceptance (PR wording); nothing code-level to re-verify. Skips remain `Passed`-silent under ctest by design; direct-run skip reasons confirmed loud in the no-numpy probe lineage (my runs all had numpy, 5/5 executed). |

The fix-closure appendix's claims all reproduced exactly (counts 1039/45, added 2+2, 0 markers, 0 duplicates, 7/7 green) — the appendix is honest.

## New-issue hunt results

- **Regexes vs adversarial CMake**: multiline `add_executable(\n  r2_ml_target_nextline\n  src.cpp)` — target on the next line IS captured (my master-advance probe detected its loss, proving it entered the base set). `add_test(NAME` on one line with the name on the next is structurally covered by the same `\s*` crossing newlines. Quoted names → N1. `${var}`/foreach-computed names correctly skipped.
- **Python harness**: socket timeouts bounded at every layer (listener accept 15 s, conn 15 s, per-request deadline 15 s, worst case well under the 180 s ctest TIMEOUT); `addCleanup(self.harness.close)` is registered BEFORE `connect()` (…:213-214), so a failed handshake still terminates→wait(5)→kill→wait — no daemon zombie on failure paths (pgrep clean after 4 mutation suites + ctest). FixturePlugin identity semantics are sound: the fixture's `cb_%d % id(action)` and the daemon's `cb_{id(action)}` are computed on the SAME object inside the daemon process, and the cb-id file is written in-daemon, so no cross-process id() mismatch exists. Daemon stdout/stderr pipes cannot fill in these scenarios: the daemon writes stderr only on error paths (9 `sys.stderr.write` sites, all failure branches; no per-request logging). Marker/plugin/temp dir cleanup missing → N3.
- **Typed-gates test**: no worker PID leak. Happy path shuts each variant's session down inside the loop (test:263-264). Failure path: a failed REQUIRE throws, Catch2 unwinds the test case, the local `shared_ptr<PluginHostProcessSession>` refcount hits zero and `~PluginHostProcessSession()` runs `killProcess("session destroyed")` (src/plugins/host/plugin_host_session.cpp:118-121) — no Stack/destructor gap exists. Empirically zero `exprs_plugin_host_worker` processes after ctest + 3 direct binary runs. Gates the test pins confirmed in production code at src/plugins/host/plugin_host_worker_main.cpp (E1002: :1048/:1096/:1118/:1137; E6002: :640/:672/:1392/:773/:510-531; malformed-tensor typed envelope :914; refused load leaves `instanceValid` false so a later valid load succeeds :1036-1039).
- **Native test**: EnvGuards are constructed BEFORE any qputenv (test:131-139), so PATH/session vars are captured pre-overwrite — no construction-order bug; restore mirrors had-value via qputenv/qunsetenv. QTemporaryDir teardown while the detached probe shell may still live is benign on Linux (the probe file is already complete; unlinking a live process's cwd is legal). `pwd`-symlink nit → N6. Production selection order (debian → gnome/unity → kde → xterm, `QProcess::startDetached(term, {}, path)`) matches both test cases (src/native/linux/qgslinuxnative.cpp:133-160).
- **Gate exit codes**: argparse errors exit 2 (matches docstring); findings exit 1; missing-git traceback exits 1 → N5.
- **ctest registration shape**: TIMEOUTs 180/120 s vs actual 2.7/1.1 s — sane. ENVIRONMENT_MODIFICATION (from `sicnu_set_test_env`, cmake/SicnuTestEnv.cmake:277-283; built values at build-E/tests/CTestTestfile.cmake:1129: QT_QPA offscreen, PYTHONHOME=/usr, PYTHONPATH prepends, LD_LIBRARY_PATH prepend) does not break the daemon child: the harness rebuilds PYTHONPATH from its own live sys.path (test:119-121), which includes /home/kevin/r23e-pysite, so numpy propagates; LD_LIBRARY_PATH pinning is inert for a pure-python child. The two C++ suites run without env mods and pass.
- **Mutation checks (independent bite verification)**, scratch copy of the daemon, real test file unmodified: neutering `callbacks.pop` → menu-callback test FAILS with the exact #1609 discriminator; neutering `algo_executors.pop` → stale-id test FAILS with the stale algorithm actually executing (`{"count": 7, "mean": 2.5}`); `_json_safe` → identity → 2 numpy tests FAIL. All three R1 mutation claims reproduced from scratch.

## Runs performed

1. `git diff --name-status origin/master...HEAD` → 5 paths, whitelist-clean; `--numstat` tests/CMakeLists.txt 59/0 (append-only). Worktree `git status --porcelain` clean throughout; HEAD b22f7ddd untouched.
2. Scratch (throwaway clone /home/kevin/r2-scratch): marker probes (.md nested/root/deep, .txt control, .MD uppercase); master-advance subset battery (script-test + 2 targets + quoted + multiline); quoted-add_test-drop (4 lines) → silent exit 0; branch-aware edge battery (14 shapes + crash probe); three daemon mutations; tempdir-leak count; exit-code battery (argparse / no-git / corrupt .git).
3. Worktree gate, registered form: `python3 scripts/check_merge_registration_integrity.py --repo-root . --base origin/master --head HEAD --require-refs` → exit 0, `1039 targets + 45 add_test names (base 1037/43; lost 0+0; added 2+2)`, 0 markers, 0 duplicates.
4. `ctest --test-dir build-E -R r23_e` (QT_QPA_PLATFORM=offscreen, PYTHONPATH=/home/kevin/r23e-pysite) → **7/7 Passed** (10.2 s).
5. `build-E/tests/test_r23_e_native_terminal` ×10 → 10 pass, 0 fail. `build-E/test_r23_e_plugin_typed_gates` ×3 → 3 pass, 0 fail.
6. Zombie sweeps before/after every battery: `pgrep -af "worker_daemon|exprs_plugin_host_worker|test_r23_e"` → none, every time.

---

## R2 修复闭环（开发者回填，2026-10-27，commit cf3bfd04）

| 发现 | 处置 | 验证 |
|---|---|---|
| N1 (P2) 引号 add_test 名双盲（master 现存 4 处） | 捕获交替式取整引号 token；`_normalize_name` 剥引号入集 | scratch 丢失 'Q Loss Probe' → exit 1 精确点名；引号名粘贴两次 → 2；live base 计数 47→49 且 master 0 finding |
| N2 (P3) .MD 大写未排除 | git 模式加 `:!*.MD`；磁盘模式 lower() 比较 | .MD 含标记 → 0 命中；.txt 对照 → 检出 |
| N3 (P3) python 套件每次泄漏 3×5 临时目录 | marker/plugin 目录 addCleanup；harness 清 socket 目录 | /tmp/r23e_* 计数 BEFORE=0 AFTER=0 |
| N4 (P3) 单行 if 吞注册+栈漂移；scope 与其 if 内同名假阴性 | 逐行按关键字序 token 化；新增 scope-vs-own-if 共存规则 | 单行 if 注册计数且栈平衡；one-liner+顺序=2；coexist=2；master 0 duplicate 回归不变 |
| N5 (P3) git 缺失 traceback exit 1 | git() 捕 OSError → 显式 exit 2 | PATH 置空目录 → **exit 2** |
| N6 (P4) CRLF `=======\r` 模式分歧 | git grep 正则加 `\r?` | 正则级闭合（与磁盘模式一致） |
| N6 其余（pwd -P 符号链接等） | 记录不改（本 Linux 车道无影响） | — |

R2 复验组合全绿：live 门（--require-refs）exit 0；N1/N4 单元 8 用例 OK；7/7 ctest 3.10s；temp 零泄漏。
