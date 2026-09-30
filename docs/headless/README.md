# Headless CLI 3.0

`sicnu_geo_rs_cli` is the automation entry point for agents, scripts and
CI-style pipelines. The legacy flag surface (`--pipeline --list --schema
--resume --list-runs --export-catalog ...`) is preserved verbatim; the
command layer below is additive.

## Commands

The table is the complete command surface — `tests/test_cli_command_surface.cpp`
gates it against `isCliCommand()`/`dispatchCliCommand()` in
`src/cli/cli_commands.cpp`, so a command added to the code without a row (or a
row without a dispatch branch) fails the suite.

| command | sub-commands |
|---|---|
| `algorithms` | `list`, `search [text] [filters…]` — text or at least one filter is required (`--group g`, `--tag a,b`, `--purpose p`, `--task f`, `--modality m`, `--input-type T`, `--output-type T`, `--large-raster-safe`, `--limit n`, `--cursor n`), `schema <id>` |
| `run` | `<operator-id> [--param k=v ...] [--params-file f]` |
| `pipeline` | `run <file.json>`, `validate <file>`, `resume <run_id>` |
| `workflow` | `run <file>`, `validate <file>`, `list-runs`, `resume <id>` |
| `plugin` | `list`, `validate <plugin-dir>`, `doctor <plugin-dir>`, `test <plugin-dir>`, `enable <id>`, `disable <id>`, `install <package-dir>`, `uninstall <id>`, `inspect <id>`, `index`, `debug-bundle <id>` |
| `models` | `list`, `inspect <name>` |
| `catalog` | `export <dir>` |
| `project` | `info`, `validate`, `health`, `search`, `migrate`, `relink`, `lineage`, `import`, `export-manifest`, `audit` (each `<file.qgz\|.qgs>`) |
| `data` | `inspect`, `doctor`, `probe`, `capabilities`, `product describe`, `plan`, `stac <dataset>`, `identity <url>`, `cache status\|clear\|<url> [--bytes N]`, `cube plan\|window <spec.json> [-o out.tif]`, `mirror materialize\|stats …` |
| `data-providers` | (lists registered providers) |
| `dataset` | `create`, `inspect`, `validate`, `diff`, `stats`, `list`, `version`, `label-schema`, `split`, `leakage` |
| `experiment` | `create`, `inspect`, `compare`, `list`, `run` |
| `reproduce` | `export`, `validate`, `inspect` |
| `lab` | `--lab <id\|.rules.json> --grade <artifact>`, `--lab <id> --batch <dir>`, `--self-check`, `--report` |
| `env-doctor` | (no sub-command; `--json` only — see `docs/deployment/env-doctor.md`) |
| `tools` | `list`, `search <query>`, `schema <tool-id>` |
| `batch` | `run <manifest.json\|jsonl> [--fail-fast] [--dry-run]`, `validate <manifest.json\|jsonl>` |
| `session` | `<action>` — `run`, `resume`, `reconcile`, `status`, `timeline`, `export`, `pause`, `cancel`, `clear-pause`, `clear-cancel`, `approve-repair`, `clear_pause`, `clear_cancel`, `actions` (agent ops driver over OperationsCoordinator) |
| `passport` | `--path <file> [--json] [--teaching] [--diff <passport.json>]` |

Global flags are parsed by the command layer, so they must appear **after** the
command (`sicnu_geo_rs_cli algorithms list --json`); placed before it they reach
the legacy flag parser and are rejected as unknown options. `--offline` is the
one exception — it is stripped before routing and is accepted in any position.

## Machine-readable output

| flag | effect |
|---|---|
| `--json` | one JSON envelope on stdout: `{"ok":bool,"command":str,"data":...,"diagnostics":[...],"error":str?,"api_version":"3.0"}` |
| `--json-lines` | NDJSON records for multi-record results |
| `--quiet` | suppress non-essential output |
| `--progress-json` | NDJSON `{"type":"progress","percent":N,...}` on **stderr** |

stdout carries only the final envelope in `--json` mode; diagnostics and
progress go to stderr, so `| jq` pipelines are safe.

`algorithms search` runs the same authoritative engine as MCP
`search_algorithms`: all filters match declared descriptor metadata (AND
across fields, comma lists are ANY-of, case-insensitive), results share one
deterministic ranking, and `data` is `{algorithms, count, total, limit,
cursor, next_cursor, hints?}` — `hints` (zero hits only) carries the declared
filter vocabulary and closest id suggestions.

## Exit codes (stable contract)

| code | meaning |
|---|---|
| 0 | success |
| 1 | unclassified failure (legacy value preserved) |
| 2 | validation failure (schema/contract) |
| 3 | execution failure (operator/workflow step failed) |
| 4 | cancelled (SIGINT/SIGTERM or cooperative cancel) |
| 5 | missing dependency (unknown algorithm/model/plugin) |
| 6 | invalid input (malformed arguments/unreadable files) |
| 7 | runtime unavailable (core failed to initialize) |

## Error message format (stable contract)

Every command-level error is a four-tuple: **code + one-sentence reason +
expected/actual + suggested action**.

In text mode it is one stderr line:

```
[E-6:INVALID_INPUT] --dataset-db <path> is required; expected: --dataset-db <path>; hint: usage: dataset <subcommand> --dataset-db <path> [options]
```

`expected:`/`actual:` segments are present only when the failure is a
mismatch or names a resource; `hint:` carries the usage line or the fix.
In `--json` mode the envelope keeps its published fields and adds the same
tuple as an object:

```json
{"ok":false,"command":"dataset","error":"--dataset-db <path> is required",
 "error_details":{"code":"E-6:INVALID_INPUT","expected":"--dataset-db <path>",
                  "hint":"usage: dataset <subcommand> --dataset-db <path> [options]"},
 "data":null,"api_version":"3.0"}
```

The `E-<exit>:<SYMBOL>` code is anchored to the exit-code table above, so
scripts can match errors without parsing prose. Unknown-subcommand errors
carry the full valid vocabulary in `error_details.expected`
(e.g. `create|inspect|validate|...`).

This contract is stable: the fields above never change shape. The
agent/MCP surface uses a different error envelope
(`{success:false,error:{code,category,…}}`, the HarnessError taxonomy); the
two are bridged by additive adapters, never by converging this table — the
rationale, the mapping, and the deprecation-gated path to a future
`api_version` 4.0 envelope are recorded in
[docs/adr/0178-cli-error-contract-vs-agent-envelope-adapter.md](../adr/0178-cli-error-contract-vs-agent-envelope-adapter.md).

## Examples

```bash
sicnu_geo_rs_cli algorithms search ndvi --json
sicnu_geo_rs_cli run 'demo:stats' --param 'values=[1,2,3]' --json
sicnu_geo_rs_cli workflow validate my-workflow.json
sicnu_geo_rs_cli plugin doctor ~/.local/share/sicnu_geo_rs/plugins/org.example.cpp-operator-demo --json
```
