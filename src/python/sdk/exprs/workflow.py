"""Workflow builder and runner over the public workflow schema v1.

The builder produces exactly the same versioned document the C++ engine and
the CLI consume (see docs/schemas/workflow-schema-v1.md). No execution logic
lives here — ``run()`` hands the document to the headless core.
"""

from __future__ import annotations

import json
from typing import Any, Dict, Optional

from .client import ExprsClient, ExprsError, default_client

SCHEMA_VERSION = 1


class WorkflowStep:
    def __init__(self, builder: "Workflow", step_id: str, operator_id: str) -> None:
        self._builder = builder
        self.step_id = step_id
        self.operator_id = operator_id

    def output(self, port: str = "output") -> str:
        """Reference to this step's output port ("$<step>.<port>")."""
        return f"${self.step_id}.{port}"


class Workflow:
    def __init__(self, workflow_id: str, title: str = "", workspace_kind: str = "") -> None:
        self.document: Dict[str, Any] = {
            "schema_version": SCHEMA_VERSION,
            "id": workflow_id,
            "title": title or workflow_id,
            "steps": [],
        }
        if workspace_kind:
            self.document["workspace_kind"] = workspace_kind

    def add(self, operator_id: str, step_id: str, **params: Any) -> WorkflowStep:
        """Adds an operator step. ``$step.port`` values in params become the
        declared inputs (placeholder grammar of the execution engine)."""
        if any(step.get("id") == step_id for step in self.document["steps"]):
            raise ValueError(f"duplicate step id: {step_id}")
        step: Dict[str, Any] = {"id": step_id, "operator": operator_id, "params": {}}
        inputs = []
        for key, value in params.items():
            if isinstance(value, str) and value.startswith("$"):
                inputs.append({"port": key, "source": value})
            else:
                step["params"][key] = value
        if inputs:
            step["inputs"] = inputs
        self.document["steps"].append(step)
        return WorkflowStep(self, step_id, operator_id)

    def to_json(self) -> Dict[str, Any]:
        return self.document

    def validate(self, client: Optional[ExprsClient] = None) -> Dict[str, Any]:
        """Server-side validation (public schema + engine semantics)."""
        return self._validate_via_file(client or default_client())

    def _validate_via_file(self, client: ExprsClient) -> Dict[str, Any]:
        import tempfile
        import os

        document = json.dumps(self.document)
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as handle:
            handle.write(document)
            path = handle.name
        try:
            envelope = client.invoke(["workflow", "validate", path])
            return envelope["data"] or {"valid": True}
        finally:
            os.unlink(path)

    def run(self, client: Optional[ExprsClient] = None,
            timeout: Optional[float] = None) -> Dict[str, Any]:
        client = client or default_client()
        self._validate_via_file(client)
        import tempfile
        import os

        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as handle:
            handle.write(json.dumps(self.document))
            path = handle.name
        try:
            envelope = client.invoke(["workflow", "run", path], timeout=timeout)
            return envelope["data"] or {}
        finally:
            os.unlink(path)


def validate_workflow_file(path: str, client: Optional[ExprsClient] = None) -> Dict[str, Any]:
    envelope = (client or default_client()).invoke(["workflow", "validate", path])
    return envelope["data"] or {}
