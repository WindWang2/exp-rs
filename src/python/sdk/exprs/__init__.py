"""
exprs — ExpRS Python SDK 3.0.

Stable, user-facing Python API over the exp-rs headless core. The SDK never
re-implements C++ algorithms: every execution goes through the headless CLI
(`sicnu_geo_rs_cli`), which is the machine-readable entry point of the same
operator contract used by the GUI, the workflow engine, and the MCP tools.

Quick start:

    import exprs

    ops = exprs.search_operators("ndvi")
    schema = exprs.operator_schema("rs:ndvi")
    result = exprs.run("rs:ndvi", input="in.tif", output="ndvi.tif", index="NDVI")

    wf = exprs.Workflow("demo", "Two-step demo")
    cal = wf.add("rs:radiometric_calibration", "cal", input="in.tif", output="cal.tif")
    ndvi = wf.add("rs:ndvi", "ndvi", input="$cal.output", output="ndvi.tif")
    wf.validate()
    wf.run()

Environment:
    SICNU_EXPRS_CLI — absolute path of the CLI binary (overrides discovery).
"""

from .client import ExprsClient, ExprsError, default_client
from .operators import list_operators, search_operators, operator_schema, run
from .workflow import Workflow, validate_workflow_file
from .models import list_models
from .plugin import operator, publish_to_iface, registered_operators
from .project import open_project

__version__ = "3.0.0"
PLUGIN_API_VERSION = "3.0"

__all__ = [
    "ExprsClient",
    "ExprsError",
    "default_client",
    "list_operators",
    "search_operators",
    "operator_schema",
    "run",
    "Workflow",
    "validate_workflow_file",
    "list_models",
    "operator",
    "publish_to_iface",
    "registered_operators",
    "open_project",
]
