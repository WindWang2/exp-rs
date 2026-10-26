# R22-07 Cross-Track Findings (for the owning tracks; this track did NOT edit these files)

## 1. `src/teaching_admin/script_adapters.cpp` — `bundleBuilderRequest` passes GUI `--version` through unvalidated (#1697 defense-in-depth)
The shell/cmd guards (this PR) now refuse unsafe versions at the point of destruction, so the P1 is closed at the enforcement point. The teaching UI could still improve UX by pre-validating (`^[A-Za-z0-9][A-Za-z0-9._-]*$`) before spawning the script, so users get an inline error instead of a script exit 2. Suggested owner: teaching track.

## 2. clang-tidy `bugprone-macro-parentheses` legacy debt in shared headers
- `src/dataset/dataset_ids.h:21` (`class Name` macro arg unparenthesized)
- `src/experiment/experiment_ids.h:14` (same pattern)

Exposed when any TU including them enters the changed-files lint set (my verification TUs did). Mechanical fix: parenthesize the macro argument at both definition sites. Suggested owner: data/experiment track. With this PR's lane fix, these no longer *block* unrelated diffs (header diagnostics now restricted to changed headers), but the debt is real and worth one small PR.

## 3. Repository-root legacy one-off scripts/docs (#1683 item, root files outside this track's whitelist)
`build_r6.cmd`, `build_r8sci2.cmd`, `configure_wb7.cmd`, `PR_BODY_R7.md`, `PR_PACK.md`, … — unused by CI, misleading at the root. Recommend `git mv` to `scripts/legacy/` (or deletion) by whoever owns repo hygiene; note `.github/**` and `scripts/**` are infra-track-writable, but root files are not in this track's grant.

## 4. Third-party GitHub Actions pinning (#1683 item)
`ci.yml` references `hendrikmuhs/ccache-action@v1.2`, `jurplel/install-qt-action@v4`, `ilammy/msvc-dev-cmd@v1`, `actions/*@v4` by mutable tags. Full SHA-pinning + a Dependabot update policy is a repo-wide decision (affects all lanes; needs coordinated update workflow). This track pinned the *source clones* (qca/qtkeychain) and added top-level least-privilege permissions; action pinning remains open.

## 5. Windows compiled-test opt-in lane needs a reusable dependency-stack workflow (#1682 follow-up)
The Windows tier's dependency stack (tier3-windows-deps job + pinned vcpkg checkout + qca/qtkeychain source builds) exists only inside `ci.yml`; duplicating it in the opt-in workflow would drift immediately. Recommended: factor tier3-windows-deps + configure into a `workflow_call`-reusable workflow, then add the Windows lane to `platform-tests-optin.yml`.

## 6. Host environment memo for all seven tracks (shared machine)
- GCC 16.2.1 Debug builds of QGIS-scale TUs are currently broken on this host (dwarf2out/front-end ICEs, deterministic across the repo's stack-raise retries). Use `CMAKE_BUILD_TYPE=Release` for local evidence until the toolchain regresses back / is fixed. A pre-Oct-19 `build-dev` Debug tree proves this is environmental.
- Do not configure with ccache launchers: root CMakeLists injects `cmake/raise-compiler-stack.sh` only when no launcher is set, and without it GCC-16 stack ICEs reproduce immediately.
- Local clang-tidy: nondeterministic SIGSEGV on Qt-heavy TUs + rejects `-mno-direct-extern-access` — treat local lint as advisory.
