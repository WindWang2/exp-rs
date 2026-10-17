# r16-c-t05 — goal-loop ledger

Oracle per package: a defect is real only if a test demonstrates it on
pristine master (RED); the fix must turn that exact test green twice in a
row with the whole affected family green twice.

| Package | Oracle verdict | RED? | GREEN ×2? | Commit |
|---|---|---|---|---|
| WP1 feedback row counts_as_pass forgery on restore | fail-closed restore invariant (header: "NEVER true" except pass) | yes — 2/4 new cases failed on pristine src | yes — 4/32 ×2; family ×2 | e904ab513 |
| WP2 listSessions truncation on unreadable lab dir | documented contract "skipped, not fatal" (Listing::warnings) | yes — warnings assertion `!true` on pristine src; truncation reproduced in standalone probe | yes — 3/30 ×2; family ×2 | 25ab7f6f0 |
| WP3 session identity coherence at load | strict envelope ("never adopts partially"); store path key vs recorded identity | yes — 3 forged-id sections accepted on pristine src | yes — same suite ×2 | 25ab7f6f0 |
| WP4 unicode/read-only pins | verification only | green on pristine (expected; pin, not defect) | yes ×2 | (in WP1/WP2 suites) |
| WP5 perf | measurement only | n/a | n/a — no optimization warranted | none |

Stale attempts / rescopes:

- WP2 initial reproduction relied on readdir ORDER (denied dir created
  first). Probe showed tmpfs returns reverse creation order → the first RED
  run passed the truncation assertions. Rescoped the deterministic oracle
  to the missing-warning contract (always violated pre-fix, order-free)
  while keeping the completeness assertions. One re-run cycle, then stable.
- Suspected defects investigated and closed as 未复现/未修 (recorded in
  IMPLEMENTATION.md "Known limitations"): concurrent `create()` sequence
  TOCTOU (design note — fix would change create/persist semantics beyond
  minimal-surgical scope); dock ignoring failed saves (out of write scope,
  `src/app/**`); `is_regular_file()` throwing overload in listing race
  (no deterministic oracle); `fromLabSpec` out-of-range currentIndex clamp
  (no producer can supply it; fail-closed already).

All targeted families were green on pristine src BEFORE any change
(nothing papered over) and green twice after both commits with identical
pass counts.
