# REVIEW_R2 — second independent review (fresh subagent, R1-blind)

- Reviewer: general-purpose subagent #2, given no R1 context; read the updated diff (/tmp/r22-infra-review2.diff) + new files + planning docs; read-only.
- Verdict: **APPROVE** — 0×P0/P1, 1×P2, 11×P3 ("residual risks are loud-failure watch items and consciously documented trade-offs, none of which block merge").

## Findings & disposition

| # | Sev | Finding | Disposition |
|---|---|---|---|
| 1 | P2 | macOS install smoke assumes brew dylibs resolve with zero rpath help; if any brew lib uses @rpath the tier3 lane goes (loudly) red | **MITIGATED**: smoke now sets `DYLD_FALLBACK_LIBRARY_PATH` to brew lib dirs — consulted AFTER @loader_path, so the relocatability proof is untouched while external deps resolve; if @rpath-style linkage still trips, the lane fails loudly (watch item, RISK_REGISTER #11) |
| 2 | P3 | Windows smoke proves loader, not Qt-in-tree presence | accepted & named (comment already modest) |
| 3 | P3 | `[A-Za-z0-9]` case ranges are collation-interpreted under UTF-8 (no traversal bypass possible, but not byte-exact) | **FIXED**: check_version forces LC_ALL=C |
| 4 | P3 | delayed-expansion `!` mangling in .cmd VERSION is consistent (probe & BUNDLE mangle identically) — contract not literally honored, no path divergence | accepted (cosmetic; fail-safe direction) |
| 5 | P3 | quote in .cmd VERSION → parse error exit 1 (fail-closed) instead of clean refusal | accepted (caller-controlled; nothing deleted) |
| 6 | P3 | twins differ: .sh validated version during --verify, .cmd didn't | **FIXED**: .sh's explicit-version check moved into the assemble path (verify mode never deletes; version irrelevant there) |
| 7 | P3 | semver `+` build tags now refused — undocumented behavior change | **FIXED**: comment in check_version documents the refusal |
| 8 | P3 | #1699 hang-guard sites (2s→10s ×2, 50ms→500ms) are a real detection-power reduction (perf regression in those windows now invisible in any lane) | consciously owned: structural oracles carry the behavior (verified by reviewer); in-line comments + PR body note |
| 9 | P3 | header-filter '^$' on .cpp-only diffs disables all header diagnostics (gate-coverage reduction vs master) | owned: matches the lane's documented "changed files only" contract; kills legacy-header flooding; lane owner sign-off noted in PR body |
| 10 | P3 | opt-in workflow polish: no permissions block; ctest lacks tier1 env parity; empty targets input edge | **FIXED** (permissions + SICNU_MODELS_DIR); empty-input edge accepted (manual dispatch) |
| 11 | P3 | review-diff bookkeeping (staged file omitted) | process artifact only; reviewer verified the registration block in-worktree and found it correct |
| 12 | P3 | test header prose overstated the RED pin (union-of-guards reality) | **FIXED**: header rewritten to "pinned by the UNION of the two guards" |

## Reviewer's independent verification highlights (from their report)

- Whitelist paper-fuzz (separators, `.`/`..`, absolute, backslash, space, `;`, `$()`, backticks, tab/newline) — every class refused; no whitelisted string can lexically escape `--out` without `/`.
- Containment boundary `/a/b` vs `/a/bcd` handled; symlinked/dangling/trailing-slash `--out` never spuriously refused (same-mode comparison confirmed).
- set -eu idioms in the test are the exempt AND-list forms; trap expansion safe; every leg non-vacuous.
- .cmd errorlevel plumbing (dynamic `if errorlevel`, `exit /b 2` through call, `goto :eof` success) traced correct; env-var PowerShell probe injection-proof.
- CI Windows smoke cannot be satisfied by an incidental failure (guard exits 2, build-dir paths exit 1) — R1-1 fix validated.
- CMake: RPATH block precedes every add_subdirectory; all lib destinations literally `lib` so `$ORIGIN/../lib` correct; no EXPORT conflict for worker install.
- SHA pins independently dereferenced via GitHub API (annotated qca tag → df5171e3…, lightweight qtkeychain tag → e63da286…, both match).
- agent_ops 200ms/500ms split traced against `startWorker` implementation (async reaper; join-without-cancel blocks ≥500ms).
- Rename completeness re-verified full-tree case-insensitively; `#pragma once` per repo style.
- No gate deletions vs master; tier1 install smoke strictly stronger (RPATH load-bearing, patchelf-RED proven).
