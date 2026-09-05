"""Model catalog access (read-only) over the headless core."""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from .client import ExprsClient, default_client


def list_models(client: Optional[ExprsClient] = None) -> List[Dict[str, Any]]:
    """All catalog models with their readiness verdicts."""
    envelope = (client or default_client()).invoke(["models", "list"])
    return envelope["data"] or []


def model(name: str, client: Optional[ExprsClient] = None) -> Dict[str, Any]:
    envelope = (client or default_client()).invoke(["models", "inspect", name])
    return envelope["data"] or {}
