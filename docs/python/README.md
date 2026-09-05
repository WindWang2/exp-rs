# ExpRS Python SDK

`import exprs` — the stable Python surface over the headless core. The SDK
drives the same operator contract the GUI uses; **no algorithm is
re-implemented in Python**.

## Install / import path

- Bundled: `share/sicnu_geo_rs/python/` (the isolated worker adds it to
  `sys.path` automatically).
- System: point `SICNU_EXPRS_CLI` at the CLI binary and put
  `src/python/sdk/` on `PYTHONPATH`.

## Core API

```python
import exprs

# discovery
exprs.search_operators("ndvi")
exprs.operator_schema("rs:ndvi")
exprs.list_models()

# execution
result = exprs.run("rs:ndvi", input="in.tif", output="ndvi.tif", index="NDVI")

# workflows (public schema v1 — same document the engine consumes)
wf = exprs.Workflow("demo.optical", "Optical prep")
cal  = wf.add("rs:radiometric_calibration", "cal", input="in.tif", output="cal.tif")
ndvi = wf.add("rs:ndvi", "ndvi", input=cal.output(), output="ndvi.tif")
wf.validate()
result = wf.run()

# projects (read-only introspection)
project = exprs.open_project("demo.qgz")
print(project.layers)
```

Errors raise `exprs.ExprsError` with stable attributes: `exit_code`,
`exit_kind` (`"validation_failure"`, `"cancelled"`, ...), `diagnostics`.

## Transport notes

One CLI process per call — boots the full registry stack (~seconds), so
batch work through workflows instead of per-file `run()` calls. Progress
and logs stream on the client's stderr channel of the invoked process.

## Python operator authoring

See [authoring.md](authoring.md): the `@exprs.operator` decorator derives
descriptor + schema + registration, and execution happens inside the
isolated Python worker — never the GUI process.
