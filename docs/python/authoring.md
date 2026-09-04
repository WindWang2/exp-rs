# Python Operator Authoring (`@exprs.operator`)

## Declare

```python
import exprs

@exprs.operator(
    id="org.demo:index",
    display_name="Demo Index",
    group="python",
    inputs=[{"name": "values", "type": "json", "required": True}],
    outputs=[{"name": "output", "type": "json"}],
)
def index(ctx, params):
    ctx.report_progress(0.5, "halfway")
    if ctx.is_cancelled():
        return {"success": False, "cancelled": True}
    values = [float(v) for v in params["values"]]
    return {"success": True, "output": sum(values) / len(values)}
```

The decorator derives the descriptor, schema and registration entry — one
declaration surfaces in the Processing catalog, Workflow engine, CLI and
MCP discovery, exactly like a C++ operator.

## Ship it as a plugin

Package layout (see `examples/plugins/python-operator-demo/`):

```
my-python-plugin/
├── plugin.json              # entrypoint_kind: "python", python.module: ...
└── python/
    └── my_python_plugin.py  # classFactory + @exprs.operator declarations
```

The isolated worker imports the module and calls `classFactory(iface)`
(QGIS-style). Publish decorated operators with:

```python
def classFactory(iface):
    exprs.publish_to_iface(iface)
    return MyPlugin(iface)   # object with initGui()/unload()
```

`publish_to_iface` binds each operator through
`iface.registerProcessingAlgorithm(...)` → the host's `py:` executor → the
AtomicAlgorithmRegistry. Execution, timeouts and crash recovery stay inside
the worker pool: a Python crash can never take the GUI down.

## Execution semantics

- `ctx.report_progress(p, msg)` — throttled progress channel
- `ctx.is_cancelled()` — cooperative cancellation (TaskCenter cancel
  propagates through the bridge)
- declared outputs are validated by the descriptor; failures raise or
  return `{"success": false, ...}`
