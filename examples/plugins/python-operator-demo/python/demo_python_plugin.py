"""Demo Python operator plugin (isolated worker payload).

Loaded by the PythonPluginHost worker: the daemon imports this module and
calls ``classFactory(iface)`` (QGIS-style convention). ``publish_to_iface``
binds every ``@exprs.operator`` declaration below to the host's Python
algorithm registry (``py:`` executor path).
"""

import exprs


@exprs.operator(
    id="demo:py_norm",
    display_name="Demo Python Normalize",
    group="python",
    description="Min-max normalizes the numeric array parameter 'values'.",
    inputs=[{"name": "values", "type": "json", "required": True}],
)
def normalize(ctx, params):
    values = params.get("values")
    if not isinstance(values, list) or not values:
        raise ValueError("parameter 'values' must be a non-empty list of numbers")
    numbers = [float(v) for v in values]
    lo, hi = min(numbers), max(numbers)
    ctx.report_progress(0.5, "normalizing")
    if ctx.is_cancelled():
        return {"success": False, "cancelled": True}
    spread = (hi - lo) or 1.0
    normalized = [(v - lo) / spread for v in numbers]
    ctx.report_progress(1.0, "done")
    return {"success": True, "normalized": normalized, "min": lo, "max": hi}


def classFactory(iface):
    """Worker entry point: publish decorated operators to the host."""

    class _Plugin:
        def __init__(self, iface):
            self._iface = iface

        def initGui(self):
            published = exprs.publish_to_iface(self._iface)
            self._iface.messageBar() if hasattr(self._iface, "messageBar") else None

        def unload(self):
            pass

    return _Plugin(iface)
