# ADR 0178: CLI error contract vs the agent/MCP error envelope — adapter first, versioned bump later

Status: accepted · Branch `fix/issue-1394-arch-leftovers` · Baseline `origin/master@1e28de8677` · Issue #1394 (item 4)

## Context

Two machine-readable error contracts coexist and have drifted:

1. **The CLI contract (published, stable).** `docs/headless/README.md:61-100`
   pins the exit-code table (0-7) and the error format: text mode's
   `[E-<exit>:<SYMBOL>] <reason>; expected: …; actual: …; hint: …` line and
   `--json` mode's additive `error_details` four-tuple
   (`{code, expected?, actual?, hint?}`) on top of the
   `{"ok":bool,"command":…,"error":str?,"data":…,"api_version":"3.0"}`
   envelope. Scripts match on the process exit code, on the `E-n:SYMBOL`
   token, and on `error_details` fields — all three are declared stable.
   The shape lives in `sicnu::cli::CliErrorDetails`
   (`src/cli/cli_commands.h:55-79`) with `exitCodeSymbol()`
   (`src/cli/cli_commands.h:34-48`).

2. **The agent/MCP harness contract.** `sicnu::agent::harness::HarnessError`
   (`src/agent/harness/harness_error.h:115-134`) carries a closed taxonomy of
   symbolic codes (`EXECUTION_FAILED`, `INVALID_PARAMETER`, `CANCELLED`, …)
   plus category/retry policy, and `errorEnvelope()`
   (`src/agent/harness/harness_error.h:139-142`) wraps it as
   `{success:false, error:{message, code, category, retryable, details,
   recoverable, suggested_actions}}` — the SpatialToolResult shape every
   agent-facing surface speaks.

The divergence is structural, not cosmetic: `ok` vs `success`; `error` as a
prose string vs a structured object; a numeric-anchored `E-n:SYMBOL` token vs
the symbolic taxonomy; exit code duplicated in the process return value vs
`category`/`retry_class` carried on the wire. A third, ad-hoc copy of the
mapping also exists in `commandAgentSession`
(`src/cli/cli_agent_ops_commands.cpp:184-197`), which hand-translates the
OpsDriver wire doc's `{ok, error}` codes (`MISSING_ARGS`,
`SEAMS_UNAVAILABLE`, `CORRUPTED_OR_MISSING_JOURNAL`, …) into CLI exit codes
with an if/else chain, and drops the `error_details` four-tuple entirely on
that path (bare `error` string, no code token). The harness side already has
the right inbound idiom — `normalizeLegacyError()`
(`src/agent/harness/harness_error.cpp:236-262`) normalizes legacy code
strings through an alias table and preserves unmapped codes in
`details.legacy_code` — but the OpsDriver codes are absent from that table,
so they degrade to `EXECUTION_FAILED` on the agent surface.

## Decision

**Do not converge the two contracts.** The CLI contract is a published
stable interface; replacing `ok`/`error`-string/`E-n:SYMBOL` with the
`{success, error-object, taxonomy}` shape is a BREAKING change for every
script and `| jq` consumer of `--json`, and `api_version` would have to move
off 3.0 to even signal it. Convergence buys uniformity of vocabulary, not
new capability.

**Converge through an adapter, additively, in both directions:**

1. **Inbound (agent reads CLI failures).** The CLI↔harness boundary uses ONE
   table-driven mapping shared with `normalizeLegacyError()`: extend the
   harness alias table with the OpsDriver/session codes (so
   `SEAMS_UNAVAILABLE` becomes a typed harness code instead of
   `EXECUTION_FAILED` + `legacy_code`), and express the CLI exit-code
   mapping (`ExitCode` → taxonomy code/category/retry class) as data beside
   `exitCodeSymbol()` rather than as another if/else chain in
   `cli_agent_ops_commands.cpp`. Placement for the implementation:
   `src/cli/cli_commands.h` (beside `CliErrorDetails`, table-driven,
   header-inline like `exitCodeSymbol()`) plus the alias rows in
   `src/agent/harness/harness_error.cpp`; `commandAgentSession` then consumes
   the table and starts populating `CliErrorDetails` on the OpsDriver path,
   which is currently the one producer that bypasses the four-tuple.
2. **Outbound (CLI surfaces harness failures).** When a CLI failure
   originates from a `HarnessError`, `CliErrorDetails` may carry the
   classification through and `finish()`'s `error_details` may gain
   ADDITIONAL optional fields (`category`, `retry_class`, `recoverable`) —
   purely additive, like Track 14 WP-B's own `error_details` introduction,
   which kept the pre-Track-14 envelope byte-identical
   (`src/cli/cli_commands.h:94-99`). The published four-tuple, the exit-code
   table and `api_version` stay untouched.

**The versioned bump stays on the shelf as the follow-up path**, gated on a
deprecation window: a future `api_version` 4.0 (or an explicit
`--error-format agent` flag) may adopt the `{success, error-object, taxonomy}`
shape while 3.0 remains served forever. Migration requirements recorded
here so the bump is mechanical when it comes: (a) map every `E-n:SYMBOL`
token to a taxonomy code via the same table the adapter introduces;
(b) keep exit codes 0-7 exactly as published — they are process-level, not
envelope-level, and consumers depend on them independent of the JSON shape;
(c) publish a side-by-side example table in `docs/headless/README.md`;
(d) land the adapter's outbound fields first so consumers can already
consume taxonomy classifications from 3.0 before the bump.

## Consequences

- Scripts and `| jq` consumers see no change: exit codes, the `E-n:SYMBOL`
  anchor and the `error_details` four-tuple are exactly as documented.
- The adapter removes the real artifact of the drift: the hand-rolled
  if/else exit-code chain in `commandAgentSession` and the four-tuple-bypass
  on the OpsDriver path — one table, one owner.
- Consumers that want agent-taxonomy classification from the CLI get it
  additively (`category`/`retry_class`/`recoverable` in `error_details`)
  without a version bump; the full envelope convergence remains a
  deliberate, deprecation-gated 4.0 decision rather than an accident of
  two teams' types.
- Cost: two vocabularies continue to exist (by decision); the table must be
  kept in sync when either side adds a code — mitigated by both tables
  living next to their contract documents
  (`docs/headless/README.md` and `harness_error.h`).
