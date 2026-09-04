# Headless CLI 3.0

`sicnu_geo_rs_cli` is the automation entry point for agents, scripts and
CI-style pipelines. The legacy flag surface (`--pipeline --list --schema
--resume --list-runs --export-catalog ...`) is preserved verbatim; the
command layer below is additive.

## Commands

| command | sub-commands |
|---|---|
| `algorithms` | `list`, `search <text>`, `schema <id>` |
| `run` | `run <operator-id> [--param k=v ...] [--params-file f]` |
| `pipeline` | `run <file>`, `validate <file>`, `resume <run_id>` |
| `workflow` | `run <file>`, `validate <file>`, `list-runs`, `resume <id>` |
| `plugin` | `list`, `validate <dir>`, `doctor <dir>`, `enable <id>`, `disable <id>`, `install <dir>`, `uninstall <id>`, `inspect <id>` |
| `models` | `list`, `inspect <name>` |
| `project` | `info <file.qgs\|.qgz>` |
| `catalog` | `export <dir>` |
| `data-providers` | (lists registered providers) |

## Machine-readable output

| flag | effect |
|---|---|
| `--json` | one JSON envelope on stdout: `{"ok":bool,"command":str,"data":...,"diagnostics":[...],"error":str?,"api_version":"3.0"}` |
| `--json-lines` | NDJSON records for multi-record results |
| `--quiet` | suppress non-essential output |
| `--progress-json` | NDJSON `{"type":"progress","percent":N,...}` on **stderr** |

stdout carries only the final envelope in `--json` mode; diagnostics and
progress go to stderr, so `| jq` pipelines are safe.

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
