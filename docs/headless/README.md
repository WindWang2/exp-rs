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
| `algorithms` | `list`, `search [text] [--group g] [--tag a,b] [--purpose p] [--task f] [--modality m] [--input-type T] [--output-type T] [--large-raster-safe] [--limit n] [--cursor n]`, `schema <id>` |
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

## Examples

```bash
sicnu_geo_rs_cli algorithms search ndvi --json
sicnu_geo_rs_cli run 'demo:stats' --param 'values=[1,2,3]' --json
sicnu_geo_rs_cli workflow validate my-workflow.json
sicnu_geo_rs_cli plugin doctor ~/.local/share/sicnu_geo_rs/plugins/org.example.cpp-operator-demo --json
```
