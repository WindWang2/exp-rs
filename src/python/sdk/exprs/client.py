"""Transport between the Python SDK and the exp-rs headless core.

The client shells out to the headless CLI in machine-readable mode
(`--json`). One process per call: the CLI boots the full processing stack
(QGIS + registries), so callers doing many small lookups should prefer
batching (workflows) or hosting a long-lived session. This is a deliberate
trade: no C++ bindings to keep in ABI sync, and the CLI is the same surface
agents and scripts use.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from typing import Any, Dict, List, Optional

EXIT_CODES = {
    0: "ok",
    1: "generic_error",
    2: "validation_failure",
    3: "execution_failure",
    4: "cancelled",
    5: "missing_dependency",
    6: "invalid_input",
    7: "runtime_unavailable",
    # Client-local condition (the CLI never exits with it): the subprocess
    # outlived the caller's deadline. Distinct from 4 "cancelled" — a
    # timeout means "no answer in time", not "the operator cancelled"
    # (SPP-11/#1806: conflating them made timeout retries indistinguishable
    # from user aborts).
    8: "client_timeout",
}


class ExprsError(RuntimeError):
    """Raised when the headless core reports a failure.

    Attributes:
        exit_code:   stable numeric exit code of the CLI (see EXIT_CODES).
        exit_kind:   stable symbolic name ("validation_failure", ...).
        diagnostics: structured diagnostics payload when available.
        data:        partial result payload when available.
    """

    def __init__(self, message: str, *, exit_code: int = 1, diagnostics: Any = None,
                 data: Any = None) -> None:
        super().__init__(message)
        self.exit_code = exit_code
        self.exit_kind = EXIT_CODES.get(exit_code, "unknown")
        self.diagnostics = diagnostics
        self.data = data


class ExprsClient:
    """Subprocess client for the headless CLI."""

    def __init__(self, cli_path: Optional[str] = None, default_timeout: float = 900.0) -> None:
        self.cli_path = cli_path or self._discover_cli()
        self.default_timeout = default_timeout

    @staticmethod
    def _discover_cli() -> str:
        candidate = os.environ.get("SICNU_EXPRS_CLI")
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
        found = shutil.which("sicnu_geo_rs_cli")
        if found:
            return found
        local = os.path.join(os.path.dirname(__file__), "..", "..", "..",
                             "sicnu_geo_rs_cli")
        if os.path.isfile(local) and os.access(local, os.X_OK):
            return os.path.abspath(local)
        raise ExprsError(
            "cannot locate the exp-rs CLI; set SICNU_EXPRS_CLI to the "
            "sicnu_geo_rs_cli binary path"
        )

    def invoke(self, args: List[str], *, timeout: Optional[float] = None,
               params: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
        """Runs `<cli> <args...> --json` and returns the response envelope."""
        # SPP-11 (#1806): an explicit timeout must be honoured exactly —
        # `timeout or default` swallowed timeout=0 into the 900 s default,
        # and a non-positive timeout is a caller bug, not a wait request.
        if timeout is None:
            effective_timeout = self.default_timeout
        elif timeout > 0:
            effective_timeout = timeout
        else:
            raise ValueError(
                f"timeout must be positive (got {timeout!r}); pass None for "
                f"the default {self.default_timeout}s"
            )
        command = [self.cli_path, *args, "--json"]
        env = dict(os.environ)
        if params:
            env.update(params)
        try:
            completed = subprocess.run(
                command, capture_output=True, text=True,
                timeout=effective_timeout, env=env,
            )
        except subprocess.TimeoutExpired as exc:
            raise ExprsError(
                f"CLI timed out after {effective_timeout}s",
                exit_code=8) from exc

        stdout = completed.stdout.strip()
        envelope: Dict[str, Any] = {}
        if stdout:
            try:
                envelope = json.loads(stdout.splitlines()[-1])
            except json.JSONDecodeError:
                envelope = {}
        if completed.returncode == 0 and envelope.get("ok"):
            envelope.setdefault("data", None)
            envelope["stderr"] = completed.stderr
            return envelope
        message = str(envelope.get("error") or completed.stderr.strip() or
                      f"CLI exited with code {completed.returncode}")
        raise ExprsError(
            message,
            exit_code=completed.returncode or 1,
            diagnostics=envelope.get("diagnostics"),
            data=envelope.get("data"),
        )


_default: Optional[ExprsClient] = None


def default_client() -> ExprsClient:
    """Process-wide shared client (lazily constructed)."""
    global _default
    if _default is None:
        _default = ExprsClient()
    return _default
