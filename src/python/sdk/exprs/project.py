"""Project access over the headless core (read-only introspection)."""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from .client import ExprsClient, default_client


class Project:
    """Handle to a project file (.qgs/.qgz) read by the headless core."""

    def __init__(self, path: str, info: Dict[str, Any]) -> None:
        self.path = path
        self._info = info

    @property
    def layers(self) -> List[Dict[str, Any]]:
        return self._info.get("layers") or []

    def info(self) -> Dict[str, Any]:
        return self._info


def open_project(path: str, client: Optional[ExprsClient] = None) -> Project:
    envelope = (client or default_client()).invoke(["project", "info", path])
    return Project(path, envelope["data"] or {})
