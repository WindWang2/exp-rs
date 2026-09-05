#!/usr/bin/env python3
"""Unit tests for the exprs Python SDK (no CLI required — fake transport)."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SDK_ROOT = Path(__file__).resolve().parent.parent / "src" / "python" / "sdk"
sys.path.insert(0, str(SDK_ROOT))

import exprs  # noqa: E402
from exprs.client import ExprsClient, ExprsError  # noqa: E402


class FakeClient(ExprsClient):
    """Records invocations and replays canned envelopes."""

    def __init__(self, responses=None):
        self.cli_path = "fake-cli"
        self.default_timeout = 10
        self.calls = []
        self.responses = responses or {}

    def invoke(self, args, *, timeout=None, params=None):
        self.calls.append(list(args))
        key = args[0] if args else ""
        response = self.responses.get(key)
        if response is None:
            raise ExprsError("no canned response", exit_code=6)
        envelope = {"ok": True, "command": key, "data": response}
        return envelope


class PythonSdkTest(unittest.TestCase):
    def test_operator_decorator_registers_metadata(self):
        @exprs.operator(
            id="org.test:calc",
            display_name="Calc",
            inputs=[{"name": "values", "type": "json", "required": True}],
        )
        def calc(ctx, params):
            return {"success": True}

        self.assertIn("org.test:calc", exprs.registered_operators())
        declaration = exprs.registered_operators()["org.test:calc"]
        self.assertEqual(declaration["display_name"], "Calc")
        self.assertEqual(declaration["inputs"][0]["name"], "values")

        ctx = exprs.plugin.OperatorContext()
        result = calc(ctx, {"values": [1]})
        self.assertTrue(result["success"])

    def test_operator_id_requires_namespace(self):
        with self.assertRaises(ValueError):
            @exprs.operator(id="nonnamespaced", display_name="Bad")
            def bad(ctx, params):
                return {}

    def test_publish_to_iface_registers_all(self):
        calls = []

        class FakeIface:
            def registerProcessingAlgorithm(self, algo_id, name="", group="",
                                            description="", execute_fn=None):
                calls.append(algo_id)

        published = exprs.publish_to_iface(FakeIface())
        self.assertEqual(published, len(exprs.registered_operators()))
        self.assertIn("org.test:calc", calls)

    def test_workflow_builder_shape(self):
        wf = exprs.Workflow("t.demo", "Demo")
        cal = wf.add("rs:radiometric_calibration", "cal", input="in.tif", output="cal.tif")
        wf.add("rs:ndvi", "ndvi", input=cal.output("output"), output="ndvi.tif")
        document = wf.to_json()
        self.assertEqual(document["schema_version"], 1)
        self.assertEqual(len(document["steps"]), 2)
        self.assertEqual(document["steps"][1]["inputs"][0]["source"], "$cal.output")
        with self.assertRaises(ValueError):
            wf.add("rs:ndvi", "ndvi")

    def test_run_serializes_params(self):
        client = FakeClient(responses={"run": {"success": True, "sum": 6}})
        result = exprs.run("demo:stats", client=client, values=[1, 2, 3], label="e2e", flag=True)
        self.assertEqual(result["sum"], 6)
        args = client.calls[0]
        self.assertIn("--param", args)
        pair = args[args.index("--param") + 1]
        self.assertEqual(pair, "values=[1, 2, 3]")

    def test_error_kind_mapping(self):
        client = FakeClient()
        try:
            client.invoke(["unknown-command"])
        except ExprsError as error:
            self.assertEqual(error.exit_code, 6)
            self.assertEqual(error.exit_kind, "invalid_input")
        else:
            self.fail("ExprsError not raised")


if __name__ == "__main__":
    unittest.main()
