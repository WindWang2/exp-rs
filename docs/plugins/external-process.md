# External Process Operator — Security Contract

External tools (GDAL CLI, R scripts, institutional executables, Python
scripts) run through `exprs::ExternalProcess` — one implementation, shared by
generic manifest operators and the plugin host.

## Guarantees

| Risk | Mitigation |
|---|---|
| Shell injection | argv-only spawn (`execvp`), never a shell; `${placeholders}` substitute into argv elements, never into a command line string |
| Zombie processes | every spawn is reaped via `waitpid` before `run()` returns |
| Runaway processes | wall-clock timeout (default 3600 s), then SIGTERM → 2 s grace → SIGKILL on the **process group** (`setsid` child) |
| Unbounded output | per-stream byte caps (stdout 8 MiB / stderr 1 MiB default); excess is drained and flagged `stdout_truncated` |
| Credential leakage via env | child inherits only `PATH HOME TMPDIR LANG`; full inheritance is opt-in (`inherit_environment`) plus explicit `environment` entries |
| Partial outputs on failure | declared outputs are redirected to temp paths and published (rename) only after a clean exit; failed runs leave nothing behind |
| Cancellation | cooperative cancel poll during execution; cancel = SIGTERM ladder on the group |

## Manifest shape

```json
"operators": [{
  "id": "gdaltools:translate",
  "display_name": "GDAL Translate",
  "inputs":  [{ "name": "input",  "type": "raster", "required": true }],
  "outputs": [{ "name": "output", "type": "raster", "required": true }],
  "external": {
    "argv": ["gdal_translate", "-of", "GTiff", "${input}", "${output}"],
    "inherit_environment": false,
    "timeout_seconds": 3600
  }
}]
```

Placeholders: `${param}` (params object), `${output}` (temp path of a
declared output port), `${plugin_dir}` (package directory). Unknown
placeholders fail the run (fail closed).

## Explicit non-goals (v1)

- No OS sandbox / seccomp: native plugins and external processes run with
  the privileges of the user. The permission declaration makes the risk
  visible; it is not a containment mechanism.
- No resource cgroup limits. Timeout + output caps bound the practical
  blast radius.
