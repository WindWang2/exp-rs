"""Python operator authoring (Phase J).

Declare operators with the :func:`operator` decorator; the descriptor,
schema and registry entry are derived from the declaration. Inside the
isolated Python worker, :func:`publish_to_iface` binds every operator
declared by the plugin module to the worker iface (``registerProcessingAlgorithm``),
which routes execution through the host's ``py:`` executor — the same
contract the GUI, CLI and MCP surfaces see. Out-of-worker, decorated
operators are inert metadata (import-safe).
"""

from __future__ import annotations

import functools
from typing import Any, Callable, Dict, List, Optional

OPERATORS: Dict[str, Dict[str, Any]] = {}


class OperatorContext:
    """Passed as the first argument to an operator implementation."""

    def __init__(self, report=None, cancelled=None, log=None) -> None:
        self._report = report or (lambda percent, message="": None)
        self._cancelled = cancelled or (lambda: False)
        self._log = log or (lambda level, message: None)

    def report_progress(self, percent: float, message: str = "") -> None:
        self._report(max(0.0, min(1.0, float(percent))), message)

    def is_cancelled(self) -> bool:
        return bool(self._cancelled())

    def log(self, level: str, message: str) -> None:
        self._log(level, message)


def operator(
    *,
    id: str,  # noqa: A002 - manifest terminology
    display_name: str,
    group: str = "python",
    description: str = "",
    inputs: Optional[List[Dict[str, Any]]] = None,
    outputs: Optional[List[Dict[str, Any]]] = None,
    schema: Optional[Dict[str, Any]] = None,
    metadata: Optional[Dict[str, Any]] = None,
    supports_cancel: bool = True,
    memory_policy: str = "full_raster",
    determinism_grade: str = "bit_exact",
) -> Callable:
    """Registers the decorated function as an exp-rs operator.

    The function signature is ``run(ctx: OperatorContext, params: dict)``
    and returns a result dict (commonly {"output": <path>, ...}).
    """

    def decorator(func: Callable) -> Callable:
        if ":" not in id:
            raise ValueError(
                f"operator id '{id}' must be 'vendor:name' (see docs/plugins/manifest-v1.md)"
            )
        declaration: Dict[str, Any] = {
            "id": id,
            "display_name": display_name,
            "group": group,
            "description": description,
            "inputs": inputs or [],
            "outputs": outputs or [],
            "schema": schema or {"type": "object", "properties": {}},
            "metadata": metadata or {},
            "supports_cancel": supports_cancel,
            "memory_policy": memory_policy,
            "determinism_grade": determinism_grade,
        }
        OPERATORS[id] = {**declaration, "function": func}

        @functools.wraps(func)
        def wrapper(ctx: OperatorContext, params: Dict[str, Any]) -> Dict[str, Any]:
            return func(ctx, params)

        wrapper._exprs_declaration = declaration  # type: ignore[attr-defined]
        return wrapper

    return decorator


def registered_operators() -> Dict[str, Dict[str, Any]]:
    """All operators declared in this process (test/introspection seam)."""
    return dict(OPERATORS)


def publish_to_iface(iface: Any, group_suffix: str = "") -> int:
    """Registers every decorated operator with an in-worker iface.

    Returns the number of published operators. The iface only needs
    ``registerProcessingAlgorithm(algo_id, name, group, description, execute_fn)``.
    """
    published = 0
    for operator_id, declaration in OPERATORS.items():
        def execute(params: Dict[str, Any], _decl=declaration) -> Dict[str, Any]:
            ctx = OperatorContext()
            return _decl["function"](ctx, params or {})

        iface.registerProcessingAlgorithm(
            operator_id,
            name=declaration["display_name"],
            group=declaration["group"] + group_suffix,
            description=declaration["description"],
            execute_fn=execute,
        )
        published += 1
    return published
