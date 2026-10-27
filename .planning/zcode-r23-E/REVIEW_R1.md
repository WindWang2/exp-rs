# REVIEW_R1 — 2026-10-27

Reviewer: independent R1 (adversarial). Base: origin/master = ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea. Branch: agent/r23-e-platform-plugin-release-20261026T170017Z (4 commits: b39942cd, 11497c6c, c9c05614, b41f9952).

Verdict: **APPROVE-WITH-FIXES**

The change is whitelist-clean, every new test drives the real production artifact it claims to guard, all 7 ctest registrations are green, and I independently reproduced the discriminating power of the Python suite via daemon mutations. The one P1 is inside the new merge-integrity gate itself: the advertised `*.md` exclusion of the conflict-marker scan is entirely non-functional (verified by counterexample on both nested and top-level `.md` files); it must be fixed or removed from the claims before this gate becomes the pre-push defense, because the first `.md` that legitimately quotes a conflict marker will false-positive the gate and teach people to bypass it.

## Findings

### F1 (P1) The `*.md` exclusion of the conflict-marker scan never works — `:(glob)!*.md` is not an exclude pathspec
- Evidence: `scripts/check_merge_registration_integrity.py:133` (`marker_specs`: `specs = [":(glob)!*.md"]`) and `:60` (`GIT_GREP_MARKER`). In git pathspec syntax the exclude shorthand must open the pathspec (`:!pattern` or `:(exclude)...`); `:(glob)!*.md` is glob magic with the literal pattern `!*.md`, a no-op include spec OR-ed with the real paths. Counterexample runs in the scratch clone (/home/kevin/r1-scratch):
  - `scripts/dev/r1_marker_probe.md` containing `<<<<<<< HEAD` → reported by the default scan AND by `--all` (`conflict marker: HEAD:scripts/dev/r1_marker_probe.md:2`).
  - `top_probe.md` at repo root → reported by `--all` (`conflict marker: HEAD:top_probe.md:1`). So even top-level `.md` is not excluded; the exclusion is 100% inert, not merely incomplete for nested paths.
  - Current tree is not yet affected: `git grep -E '^(<<<<<<< |>>>>>>> |\|\|\|\|\|\|\| |=======$)' HEAD -- '*.md'` returns nothing, which is why the registered test passes today.
- Why it matters: the docstring (`*.md excluded — prose legitimately quotes markers`), the `--all` help text (`still excluding *.md`) and the commit message all promise this exclusion. The documented failure mode (prose quoting markers) will produce a false gate failure; a gate that cries wolf gets bypassed exactly when it matters.
- Suggested fix: one line — `specs = [":!*.md"]` (default magic, where `*` crosses `/`), or `":(exclude,glob)**/*.md"`. Re-run my two counterexamples plus a clean-tree run as the acceptance test. The on-disk twin `scan_markers_on_disk` already skips `.md` correctly, so only the git-mode spec needs the fix.

### F2 (P2) The subset gate is blind to `add_test(NAME ...)`-only registrations — including two of the four this PR adds
- Evidence: counterexample CX5 in the scratch clone — deleting the entire WP2 block (`add_test(NAME r23_e_python_worker_typed ...)` plus its properties) and committing: `integrity: ... lost: 0; added: 2 ... exit=0`. `REGISTER_RE` (scripts/check_merge_registration_integrity.py:55) only matches `sicnu_add_*|qt_add_executable|add_executable`; the baseline output itself confirms it (`added: 2` — the two `add_test` registrations `r23_e_python_worker_typed` and `r23_e_merge_registration_integrity` never appear in the 1040-target set).
- Why it matters: the incident class this gate exists for (#1715 five-track merge dropping a sibling's registration block in tests/CMakeLists.txt) is exactly as likely to eat an `add_test` block as a target block; pure-script tests registered via `add_test` get no protection.
- Suggested fix: collect a second set from `\badd_test\s*\(\s*NAME\s+([^\s)$,]+)` and apply the same subset rule (shared `--allow-removed`).

### F3 (P2) Missing base ref "degrades" to a PASSING gate — the primary invariant silently disappears from CI
- Evidence: counterexample CX6: `python3 scripts/check_merge_registration_integrity.py --base no-such-ref` → stderr `integrity: ref not resolvable ... degrading to static checks`, stdout `... mode=static (no ref comparison)`, **exit=0**. The ctest-registered invocation (`r23_e_merge_registration_integrity`, tests/CMakeLists.txt:16806-16813) therefore reports Passed with the subset check unchecked; ctest only shows output on failure, so the "loud" stderr line is invisible in a green run.
- Why it matters: on any runner where `origin/master` is absent or named differently (shallow/sparse checkouts, secondary remotes), the headline defense of this script is off while everything looks healthy.
- Suggested fix: `--require-refs` flag (exit 1 when refs don't resolve) used by the CMake registration; keep the degrade for interactive/static use.

### F4 (P2) `waitForProbe` can return a half-written probe file and fail the native test spuriously
- Evidence: tests/test_r23_e_native_terminal.cpp:445-457 — the poll loop opens the probe file, reads three lines, and `return result` unconditionally once the file opens (`if (in.good()) { ... return result; }`), even when `cwd` is empty (`result.valid` stays false). The probe shell creates the file at redirect-open and writes lines 1-3 microseconds later; a poll landing in that window returns `valid=false` with no retry, and `REQUIRE(probe.valid)` (line 509/554) fails. Not observed in 15 consecutive runs (the window is ~µs against a 25 ms poll), but it is a genuine race on loaded CI machines.
- Why it matters: a flaky guard for #1618 erodes trust in the whole suite.
- Suggested fix: only return when the file is complete — `if (!cwd.empty()) { ...; return result; }` and keep polling otherwise.

### F5 (P3) `REGISTER_RE` harvests non-target junk from multi-line `sicnu_add_*` calls; `#[==[ ]==]` comments are not stripped
- Evidence: at HEAD the registered-target set contains exactly one junk name: `runtime/observability/trace_id.h`, captured from `sicnu_add_header_probes(\n  runtime/observability/trace_id.h ...)` at tests/CMakeLists.txt:11668-1169 — the open-ended `sicnu_add_[a-z_]+` family matches a non-test helper and `\s*\(\s*` crosses the newline (census run: `total: 1040, junk(names with /): 1`). Direct function probe also shows `#[==[ add_executable(longbracket_commented ... )==]` is COUNTED (only `#[[ ]]` is stripped by `uncommented()`, scripts/...:79-95), contradicting the claimed parity with check_test_registration.py.
- Why it matters: removing/renaming the first header of that list later produces a baffling `registration lost: 'runtime/observability/trace_id.h'` finding that can only be silenced with `--allow-removed <header path>`; a long-bracket-commented-out registration would false-positive the duplicate check.
- Suggested fix: either restrict the family to the actual helpers (`sicnu_add_(test|io_test|sdk_test|study_test|python_test)`) or skip captured names containing `/`; extend `uncommented()` to `#[==[...]==]`.

### F6 (P3) Git failures fail open in the gate
- Evidence: scripts/check_merge_registration_integrity.py:143-149 (`scan_markers_git` returns `[]` for every non-zero returncode — a git error is indistinguishable from "no matches") and :123-129/:184-192 (`git show` failures silently skip the file, shrinking the base set and weakening check 1 without any message). I could not trigger a live git error (my forced bad-pathspec probe returned rc=1), so this is a code-reading finding, not a demonstrated one.
- Suggested fix: treat returncode > 1 (grep) / != 0 (show) as an explicit finding ("gate could not read ...") — fail closed like the rest of the script.

### F7 (P3) Static vs git duplicate-scan surfaces differ
- Evidence: `duplicate_counts_disk` (:195-208) walks `CMakeLists.txt` AND `*.cmake` under tests/; the git-mode `duplicate_counts_git` only reads files matched by `tracked_cmake_files` (:113-120, `*.endswith("/CMakeLists.txt")`). A duplicate introduced inside an included `.cmake` file is caught only in static mode.
- Why it matters: the two modes should agree; today they don't.
- Suggested fix: add `*.cmake` to `tracked_cmake_files`' filter.

### F8 (P3) "Skips loudly" for the Python test is only true for direct runs
- Evidence: without numpy, direct run prints per-test skip reasons (verified: 5 × `skipped 'numpy unavailable — ...'`); under ctest a skipped test is just `Passed` and its stdout is hidden unless the run fails. On a Windows host with Python+numpy but no AF_UNIX the ctest line will read Passed with no visible hint. Acceptable design (documented POSIX-only daemon), but the commit's "skips loudly" claim overstates what CI dashboards show. Optional fix: none needed; or emit a `set_tests_properties(... SKIP_RETURN_CODE ...)`-style marker if silent skips ever bite.

## What I verified (commands + results)

Whitelist / shape:
- `git diff --name-status origin/master...HEAD` → exactly the 5 allowed files (1 modified: tests/CMakeLists.txt; 4 added). **No src/** change. `git status --porcelain` clean.
- tests/CMakeLists.txt change is a pure append at the tail (`@@ -16779,3 +16779,62 @@`, only `+` lines after `# --- end R22-04 registration ---`).
- All helpers/targets referenced by the new blocks exist: `sicnu_set_test_env` (cmake/SicnuTestEnv.cmake:277), `sicnu_add_sdk_test` (tests/CMakeLists.txt:180 — discovers internally, so no double-discovery in the WP1 block), `sicnu_discover_tests` (tests/CMakeLists.txt:41, accepts TEST_PREFIX + RUN_SERIAL), `isolation_plugin` (tests/CMakeLists.txt:10494, output dir matches `SICNU_TEST_ISOLATION_PLUGIN_DIR`), `exprs_plugin_host_worker` (src/plugins/host/CMakeLists.txt:37), `qgis_native` (src/native/CMakeLists.txt:64), `sicnu_plugins_hostprocess` (src/plugins/host/CMakeLists.txt:20).
- `ctest -N -R r23_e` in build-E → 7 tests, each name registered once (no duplicate discovery).

Production contracts pinned by the tests (all confirmed by reading the cited code):
- Services-object gate → E1002 (src/plugins/host/plugin_host_worker_main.cpp:1086-1098); services-member gates via `peerString` → E1002 (:1107-1121, peerString at :471-478); pluginDirectory gate → E1002 (:1131-1140); hostile manifest string/array → `PluginManifest::fromJson` total-catch → typed false → E1002 (:1045-1049; src/sdk/exprs/plugin_manifest.cpp wraps everything in try/catch).
- Execution gates → E6002: operatorId (:640), workDir (:672), toolId (:1392), provider uri (:773), model request object (:508-513), modelName (:515-519); malformed tensor → typed RESULT envelope with `success=false` and `malformed inference request` (:902-922). `instanceValid` (:1036) ensures a refused load still admits a later valid load on the same worker.
- IPC Outcome model: `enum class Status { Ok, Error, Timeout, Cancelled, ChannelClosed, ProtocolError }` (src/sdk/exprs/ipc_channel.h:73-81); worker death mid-request surfaces as ChannelClosed — so the test's exact `status == Error` assertion genuinely separates "typed refusal" from both "swallowed exception → Timeout" and "dead worker → ChannelClosed". A wrong typed code also fails the suite (`REQUIRE(refused.error.code == "E1002"/"E6002")`); error messages are only required non-empty (plus one substring) — not brittle.
- worker_daemon.py: `addPluginToMenu` derives `cb_{id(action)}` (src/python/scripts/worker_daemon.py:216) — identical to the fixture's `cb_%d % id(action)`, and the positive-path assertion validates the derivation; unload pops executors (:482-483) and callbacks (:492-493); stale py: id → -32602 (:535-543); bogus `__shm_key__` → -32603 "Shared memory segment not found" (:571-581); `_json_safe` coercion + pre-encoded reply → -32000 "non-JSON-serializable" (:627-663). The daemon is single-threaded per connection, so the trigger-marker discriminator has no write/read race (trigger runs before the ack is sent).
- qgslinuxnative.cpp:130-156: selection order debian_version → gnome/unity → kde → xterm and `QProcess::startDetached(term, {}, path)` — matches both test cases' expectations, including the KDE leg's `XDG_CURRENT_DESKTOP=KDE` → konsole and its documented debian-host skip. `NativeOpenTerminalAtPath` bit advertised (qgslinuxnative.cpp:39, qgsnative.h:48).

Runs:
- `ctest --test-dir build-E -R r23_e` (QT_QPA_PLATFORM=offscreen, PYTHONPATH=/home/kevin/r23e-pysite) → **7/7 Passed** (3.53 s).
- `build-E/tests/test_r23_e_native_terminal` × 15 → 15 pass, 0 fail.
- `build-E/test_r23_e_plugin_typed_gates` × 5 → 5 pass, 0 fail.
- `python3 scripts/check_merge_registration_integrity.py --base origin/master --head HEAD` (worktree and fresh clone) → exit 0, `1040 registered targets at HEAD (base origin/master: 1038; lost: 0; added: 2)`. `--no-git` → exit 0, static mode.

## Counterexamples attempted (and outcomes)

All destructive work in the throwaway clone /home/kevin/r1-scratch (worktree untouched); tracked worktree files never modified.

1. **Surgical single-registration loss** (delete `qt_add_executable(test_r22_agent_ghost_algo ...)` line, commit): detected exactly — `lost: 1`, exit 1; `--allow-removed test_r22_agent_ghost_algo` → exit 0. Gate works.
2. **Catastrophic loss** (accidental: my first sed deleted the whole file because the grep pattern didn't match): 1037 lost-target findings, exit 1 — fail-loud on the worst case.
3. **Duplicate registration** (`add_executable(test_r22_agent_ghost_algo dupes.cpp)` appended): `duplicate registration: test_r22_agent_ghost_algo registered 2 times`, exit 1.
4. **Conflict markers**: `<<<<<<< HEAD` / `=======` / `>>>>>>> other` appended to tests/CMakeLists.txt → all three reported; `||||||| merged` in root CMakeLists.txt → reported; `<<<<<<< HEAD` in CMakePresets.json → reported. Regex probe: `=======` (bare) matches, `======= text` correctly does not, leading-space forms correctly do not.
5. **`.md` exclusion**: marker line in `scripts/dev/r1_marker_probe.md` (default + `--all`) and in top-level `top_probe.md` (`--all`) → **both reported despite the claimed exclusion** → F1.
6. **add_test-block loss**: WP2 `add_test(NAME r23_e_python_worker_typed ...)` block deleted → exit 0, `lost: 0` → F2.
7. **Unresolvable base ref**: `--base no-such-ref` / `refs/heads/no-such-ref` → stderr note, exit 0 → F3.
8. **Regex edges** (direct module probe): plain/spaced/`qt_add_executable` names captured; `#`-comment and `#[[ ]]` registrations correctly ignored; `${t}`/foreach-computed names correctly skipped; hypothetical `sicnu_add_future_test` captured (family is open-ended as intended); `#[==[ ]==]` captured → F5.
9. **Daemon mutations** (scratch copy, real test file unmodified): removing the `callbacks.pop` loop → menu-callback test FAILS (`AssertionError: True is not false ... #1609`); `_json_safe` neutered → 2 numpy tests FAIL; removing `algo_executors.pop` → stale-id test FAILS (the stale algorithm actually executed and returned `{"count": 7, "mean": 2.5}`). The commit's "mutation-checked" claim for #1608/#1609/#755 is independently reproduced. (The C++ worker-gate mutation claim could not be reproduced without rebuilding the worker; code reading makes it plainly plausible — removing the gates lets jsoncpp throw `Json::LogicError` into a catch-free path, which the exact status/code assertions would catch.)
10. **Skip path**: run without numpy on PATH → all 5 tests skip with printed reasons (daemon imports numpy at module top, worker_daemon.py:10 — the skip is load-bearing, not cosmetic).

Not verified / accepted on evidence: cross-platform compilation of the two C++ tests beyond this Linux lane (they are correctly gated: `if(UNIX AND NOT APPLE)` for the native test; the typed-gates test is platform-portable by construction with the `#ifdef _WIN32` entrypoint spelling and the session's platform IPC).

---

## R1 修复闭环（开发者回填，2026-10-27）

| 发现 | 处置 | 验证 |
|---|---|---|
| F1 (P1) `:(glob)!*.md` 排除无效 | pathspec 改 `:!*.md`（默认 magic，`*` 跨 `/`） | 嵌套/顶层 .md 含标记 → exit 0；同标记入 cmake/ 非 md 文件 → exit 1 检出 |
| F2 (P2) add_test-only 注册子集盲区 | 新增 ADD_TEST_RE 收割，target/add_test 两集合分别做子集断言（--allow-removed 裸名匹配两类） | scratch 中删除 master 侧 add_test 块 → exit 1，finding 精确点名 |
| F3 (P2) 基 ref 不可解析时静默降级为 PASS | 新增 `--require-refs`（不可解析即 exit 1），ctest 注册接线使用之；交互式保留降级 | `--base no-such-ref --require-refs` → exit 1；不带 flag → exit 0 + 显式 stderr |
| F4 (P2) waitForProbe 半写文件竞态 | 仅当探针第三行（cwd）非空才视为完整，否则继续轮询 | 代码级修复；7/7 复跑 |
| F5 (P3) 路径型 junk 名 / `#[==[ ]==]` 注释 | `_valid_name` 拒绝含 `/` 的名字；`uncommented()` 支持长括号注释 | master 全量 0 junk；长括号内注册不计、真实注册计 |
| F5b（新，修复验证中发现）master 真实存在 if/else 双 add_test（classroom_batch_conformance / launcher_parity_conformance，tests/CMakeLists.txt:13596/13615 区） | 重复检测改分支感知：同一 if 的兄弟分支=一次注册；同分支两次=重复；兄弟 if 同名=冲突 | 三用例单元验证（alt=1 / paste=2 / sibling-ifs=2）；master 干净跑 0 duplicate |
| F6 (P3) git 失败 fail-open | git grep rc≥2 / git show rc≠0 / ls-tree 失败 → 显式 finding（fail-closed） | 代码路径闭合；未触发真实 git 错误（与 R1 同一限制） |
| F7 (P3) 两模式扫描面不一致 | tracked_cmake_files 纳入 tests/**/*.cmake | 两模式对同一 HEAD 计数一致 |
| F8 (P3) ctest 下 skip 不可见 | 接受（POSIX-only 设计已文档化）；PR_BODY 措辞改为直接运行 loud skip / ctest 显示 Passed 的诚实描述 | 文档 |

修复后完整验证：live（worktree，HEAD vs origin/master）exit 0（1039 targets + 45 add_test，added 2+2，0 marker，0 duplicate）；模拟合并腿全过/全检；ctest 7/7 复跑绿。
