"""Exercise Windows CLI help with redirected output and without console or redirection."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main(cli: Path) -> None:
    command = [str(cli.resolve(strict=True)), "--help"]
    env = os.environ.copy()
    env.pop("QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES", None)
    with tempfile.TemporaryDirectory() as cwd:
        output = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            creationflags=subprocess.CREATE_NO_WINDOW,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            timeout=30,
            check=True,
        )
        if not all(option in output.stdout for option in (b"--operator-help", b"--list-topics")):
            raise RuntimeError(f"CLI help is missing from stdout: {output.stdout!r}")
        print("Redirected CLI help printed the supported options.", flush=True)

        # Supplying even NULL handles with STARTF_USESTDHANDLES tells Qt
        # that output was redirected. Leave all three streams unspecified.
        subprocess.run(
            command,
            cwd=cwd,
            env=env,
            creationflags=subprocess.CREATE_NO_WINDOW,
            close_fds=True,
            timeout=30,
            check=True,
        )
        print("CLI help exited without a console window or redirected handles.", flush=True)


if __name__ == "__main__":
    main(Path(sys.argv[1]))
