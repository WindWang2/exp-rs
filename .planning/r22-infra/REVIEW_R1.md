# REVIEW_R1 — independent adversarial review (subagent, fresh context)

- Reviewer: general-purpose subagent, no development context, read-only access to worktree + /tmp/r22-infra-review.diff.
- Verdict: **REJECT** — 2×P1, 4×P2, 5×P3 (full text preserved in git history of this file; summary below).

## Findings & disposition

| # | Sev | Finding | Disposition |
|---|---|---|---|
| 1 | P1 | Windows bundle-guard CI smoke vacuous (any nonzero exit passes; build-dir error also nonzero) | **FIXED**: assert exit code == 2 AND stderr contains `refusing unsafe --version` (output captured, no Out-Null) |
| 2a | P1 | "#1698 oracle unexecuted — TEST_RESULTS.md still says PENDING" | **STALE PREMISE**: reviewer read the pre-update snapshot of TEST_RESULTS.md (review started before the oracle ran). The executed evidence (INSTALL_RC=0, readelf RUNPATH ×3, `env -i` --help RC=0 ×2, `env -u LD_LIBRARY_PATH ldd` resolving all project libs via `bin/../lib`, patchelf --remove-rpath RED → loader failure RC 127) is recorded in the final TEST_RESULTS.md. No code change. |
| 2b | P1 | Linux CI install smoke: aqt Qt path dropped from RUNPATH and not in ldconfig → step red for external deps | **FIXED**: run installed CLI with `LD_LIBRARY_PATH=${{ steps.qt.outputs.qt-path }}/lib`; project libs still resolve via RUNPATH (LD_LIBRARY_PATH does not shadow them — Qt dir lacks them), preserving the relocatability proof |
| 2c | P1 | Windows CI install smoke: vcpkg DLLs not deployed at install time → DLL-not-found | **FIXED**: add `-DX_VCPKG_APPLOCAL_DEPS_INSTALL=ON` to the Windows configure so install-time applocal copies vcpkg runtime DLLs into the prefix |
| 3 | P2 | `[ ! -f stale ] || true` vacuous in guard test | **FIXED**: assert stale.txt STILL EXISTS after battery |
| 4 | P2 | new files untracked / exec bit | process — `git add` explicit paths at commit; .sh added with 100755 (verified mode) |
| 5 | P2 | ctest registration runs .sh directly, breaks Windows dev ctest | **FIXED**: `if(UNIX)` + `COMMAND sh <abs script>` (interpreter invocation, repo precedent; removes exec-bit dependency) |
| 6 | P2 | opt-in ccache key forks a third macOS pool | **FIXED**: reuse tier3 key `ccache-tier3-macos-clang-Release` |
| 7 | P3 | header comment overclaims RED pin; containment check has no oracle | **FIXED**: comment corrected; new leg asserts a legal run through a SYMLINKED --out still succeeds (containment check must not spuriously fire) |
| 8 | P3 | header-filter silently matches nothing on future relative include spellings | **FIXED**: lane now echoes the filter mode + entry count when changed-headers filter is used |
| 9 | P3 | UBSan-clang/MSVC sanitizer detection gap; atof inf → UB | **FIXED**: bounded parse (strtod, accept (0, 1e6]); documented gaps in comment |
| 10 | P3 | scaled hang-guards exceed the unscaled structural timeouts in ASan lane (vacuous there) | **FIXED**: one-line comments at both exec_plane sites noting ASan-lane vacuity; Release lane enforces base |
| 11 | P3 | containment check mixed-resolution spurious refusal | **FIXED**: if either realpath falls back to raw, compare BOTH raw forms (same-mode comparison) |

Reviewer's explicitly confirmed-sound list (no action): .sh whitelist/set -eu interaction, guard dual placement, .cmd expansion-phase fail-closed analysis + errorlevel propagation, RPATH block placement/escaping/CMP0042/plugins/MSVC no-op, worker install rule, #1696 rename completeness + include fix, lint ordering, merge-base + depth-500, pwsh SHA compare, permissions placement, lifetime extension of `const QStringList &`, agent_ops structural split, hang-guard oracles.
