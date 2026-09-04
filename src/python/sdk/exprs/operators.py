"""Operator discovery and execution over the headless core."""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from .client import ExprsClient, default_client


def list_operators(client: Optional[ExprsClient] = None) -> List[Dict[str, Any]]:
    """All algorithms visible to the core (builtin + plugin)."""
    envelope = (client or default_client()).invoke(["algorithms", "list"])
    return envelope["data"] or []


def search_operators(text: str, client: Optional[ExprsClient] = None) -> List[Dict[str, Any]]:
    """Case-insensitive substring search over id/name/group/description."""
    envelope = (client or default_client()).invoke(["algorithms", "search", text])
    return envelope["data"] or []


def operator_schema(operator_id: str, client: Optional[ExprsClient] = None) -> Dict[str, Any]:
    """The algorithm's declared input schema and description."""
    envelope = (client or default_client()).invoke(["algorithms", "schema", operator_id])
    return envelope["data"] or {}


def run(operator_id: str, client: Optional[ExprsClient] = None,
        timeout: Optional[float] = None, **params: Any) -> Dict[str, Any]:
    """Executes one operator and returns its result payload.

    Parameter values are passed as strings on the command line; JSON-typed
    values (lists, numbers, booleans) are serialized so the core can parse
    them back. For complex parameter sets prefer ``params_file=``:

        exprs.run("rs:ndvi", params_file="params.json")
    """
    client = client or default_client()
    args = ["run", operator_id]
    if "params_file" in params:
        args += ["--params-file", str(params.pop("params_file"))]
    for key, value in params.items():
        if isinstance(value, (dict, list)):
            import json as _json

            rendered = _json.dumps(value)
        elif isinstance(value, bool):
            rendered = "true" if value else "false"
        else:
            rendered = str(value)
        args += ["--param", f"{key}={rendered}"]
    envelope = client.invoke(args, timeout=timeout)
    return envelope["data"] or {}
