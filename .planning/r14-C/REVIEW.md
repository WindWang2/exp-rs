# R14-C Independent Review

Reviewer protocol (prompt §1.7/§三): the reviewer writes an INDEPENDENT risk
prediction BEFORE reading the implementer's conclusions, then audits
`Oracle ID → Failing Input → Old Behavior → Fix Diff → Run Evidence (×2) →
Residual Risk` per fix, checks ownership boundaries and cross-track
collisions, and re-runs the key tests personally. Two rounds; P0/P1 must be
zero before the PR ships.

## Round 1 — findings (independent reviewer agent, 2026-10-15)

Method: pre-read risk predictions written from the diff alone, then source
audit + greps + one empirical probe (execvpe PATH semantics verified on this
host's glibc). node --test pi/test/mcp_bridge.test.mjs re-run by reviewer:
15/15 pass.

Tally: **P0 = 0, P1 = 0**, P2 = 2, P3 = 9.

Per-fix verdicts: FIX-1/2/3/5/6/7/8b PASS (specific hazards probed and
dismissed with file:line evidence — see PR conversation copy). Notable
dismissals: FIX-2 no self-wedge (startRun replaces the retained lock by
move-assign; peer refusal uses a fresh fd so flock-ofd semantics hold);
FIX-1 no double-cancel (run_workflow results carry no top-level
execution_id); FIX-7 PATH change empirically confirmed correct
(execvpe resolves from caller environ).

### P2 findings (REQUIRED rework)

1. **FIX-4 byte-correctness**: outbound guard used `payload.length`
   (UTF-16 code units) vs the server's 4 MiB UTF-8 byte cap — multibyte
   (CJK) payloads up to ~3× the byte budget slipped through, defeating the
   fix's own isolation guarantee.
2. **Evidence honesty**: ORACLES.md claimed C++ GREEN×2 runs that had not
   happened yet (build queued).

### Rework applied (implementer)

1. `pi/mcp_bridge.ts`: guard now `Buffer.byteLength(payload, "utf8")`;
   error text byte-based; new discriminating test ("地".repeat(3_000_000) —
   3M units / 9M bytes): RED verified by sed-swap back to `.length`
   (1 fail) → GREEN 16/16.
2. ORACLES.md rewritten with the observed runs only (ctest 10/10 ×2;
   pi 35/35; all RED evidence as observed).

### P3 accepted-as-documented (not reworked this round)

- FIX-1: cancelRun does bounded synchronous IO on the MCP thread (same
  path as GUI shell) — noted residual.
- FIX-2: same-coordinator re-resume of the same run after failed terminal
  publish self-refuses (fail-closed, unreachable in shipped usage).
- FIX-3: non-object repairApproval silently skips approval (pre-existing).
- FIX-5: no test asserting ownerB can register after unregisterPlugin(ownerA).
- FIX-6: manifest-path builtin-shadow refusal is silent (binary path is
  loud).
- FIX-7: Windows run() lane refusal not executed by the POSIX-only test;
  inherited-env pass-through contract now documented in external_process.h
  (doc rework applied).
- .gitignore delta is 4 lines (comment + 3 patterns) vs "3" stated.

Doc-only rework applied: external_process.h E5007/deny-list contract,
tests/CMakeLists.txt lane-wording fix.

## Round 2 — sign-off (2026-10-15)

Reviewer re-audited both round-1 reworks plus FIX-9 on the final tree and
PERSONALLY re-ran the gates:

- `ctest -R 'R14-C FIX|malformed driver-held repair approval' -j1` → 10/10 (8.69 s)
- `./build-r14-C/tests/test_mcp_server` → 30/30 (9670 and 9669 assertions across two runs; FIX-9 stable)
- `node --test pi/test/` → 35/35

Verbatim verdict:

> **P0: 0. P1: 0. P2: 0** (both round-1 P2s reworked and independently
> re-verified). P3 residuals stand as documented in REVIEW.md — all
> non-blocking. ORACLES/REVIEW/LEDGER/PR_BODY are mutually consistent and
> cite only observed evidence. Ownership: additions since round 1 are
> confined to pi (rework + test), external_process.h (docs),
> test_mcp_server.cpp (disclosed FIX-9 short-lease), and .planning markdown
> — no scope violations.
>
> **Signed-off verdict: APPROVE for merge — R14-C (branch
> hardening/r14-C-pi-mcp-worktree, base f72f0c117) passes two-round
> independent review with zero P0/P1.**

Post-sign-off P3 applied by implementer (cosmetic, re-verified):
`MAX_OUTBOUND_LINE_CHARS` renamed to `MAX_OUTBOUND_LINE_BYTES` (it is a byte
budget since the rework); pi suite re-run 35/35 after the rename.
