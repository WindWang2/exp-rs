#!/usr/bin/env python3
"""Exercise only initialize/shutdown using an already verified relocated package.

The caller owns outbound-network blocking. This starts no model request and
uses only an explicit fake credential in a fresh, private environment.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import tempfile
import threading


def smoke(prefix):
    node = prefix / "libexec/sicnu-geo-rs/node/node.exe"
    entry = prefix / "libexec/sicnu-geo-rs/pi-product/main.mjs"
    receipt_sha = hashlib.sha256((prefix / "share/sicnu-geo-rs/pi-runtime.json").read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix="sicnu-pi-relocated-") as temporary:
        # The pinned Windows Node can represent this path, but its native
        # home-directory lookup fails once USERPROFILE reaches 260 UTF-8 bytes.
        state = Path(temporary).joinpath(*(f"第{i}层 中文离线遥感影像验证工作区" for i in range(1, 8)))
        work = state / "work"
        work.mkdir(parents=True)
        poison = state / "poison-bin"
        poison.mkdir()
        for name in ("node", "npm", "python", "python3"):
            (poison / (name + ".cmd")).write_bytes(b"@echo Unexpected system runtime fallback 1>&2\r\n@exit /b 91\r\n")
        for path in (state / "AppData/Local", state / "AppData/Roaming"):
            path.mkdir(parents=True)
        env = {key: value for key, value in os.environ.items()
               if key.upper() in {"SYSTEMROOT", "WINDIR", "SYSTEMDRIVE", "COMSPEC", "PATHEXT",
                                  "PROCESSOR_ARCHITECTURE", "NUMBER_OF_PROCESSORS"}}
        env.update({"PATH": str(poison) + os.pathsep + str(Path(os.environ["SYSTEMROOT"]) / "System32"),
                    "USERPROFILE": str(state), "HOME": str(state), "TEMP": str(state), "TMP": str(state),
                    "LOCALAPPDATA": str(state / "AppData/Local"), "APPDATA": str(state / "AppData/Roaming"),
                    "PI_OFFLINE": "1", "PI_TELEMETRY": "0"})
        process = subprocess.Popen([str(node), str(entry)], cwd=work, env=env,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, encoding="utf-8", errors="strict", bufsize=1)
        output = queue.Queue()
        errors = []

        def read_output():
            try:
                for line in process.stdout:
                    output.put(line)
            finally:
                output.put(None)

        def read_errors():
            for line in process.stderr:
                errors.append(line.rstrip())
                del errors[:-16]

        stdout_reader = threading.Thread(target=read_output, daemon=True)
        stderr_reader = threading.Thread(target=read_errors, daemon=True)
        stdout_reader.start()
        stderr_reader.start()
        envelope = {"protocol": "sicnu.pi/1", "sessionId": "ci-smoke", "generation": "relocated-1", "runId": ""}

        def send(kind, seq, **fields):
            process.stdin.write(json.dumps({**envelope, "type": kind, "seq": seq, **fields}) + "\n")
            process.stdin.flush()

        def receive(kind, seq):
            line = output.get(timeout=180)
            if line is None:
                raise RuntimeError("Pi protocol exited before " + kind)
            message = json.loads(line)
            for field, value in {**envelope, "type": kind, "seq": seq}.items():
                if message.get(field) != value:
                    raise RuntimeError("Unexpected Pi " + field + " in " + kind)
            return message

        try:
            send("initialize", 1, cwd=str(work),
                 profile={"baseUrl": "https://model.invalid/v1", "model": "fixture", "apiKey": "ci-fixture-not-real",
                          "contextWindow": 16384, "maxTokens": 4096},
                 systemPrompt="Offline packaging smoke", tools=[], toolRevision="ci-empty", sdkReceiptSha256=receipt_sha)
            ready = receive("ready", 1)
            for field, expected in {"nodeVersion": "24.22.3", "piVersion": "1.5.2",
                                    "sdkReceiptSha256": receipt_sha, "toolRevision": "ci-empty"}.items():
                if ready.get(field) != expected:
                    raise RuntimeError("Relocated ready identity differs: " + field)
            send("shutdown", 2)
            if receive("closed", 2).get("reason") != "shutdown":
                raise RuntimeError("Pi did not acknowledge shutdown")
            process.stdin.close()
            if process.wait(timeout=15) != 0:
                raise RuntimeError("Pi child exit failed")
            stdout_reader.join(timeout=2)
            while not output.empty():
                if output.get_nowait() is not None:
                    raise RuntimeError("Unexpected output after Pi shutdown")
        except BaseException as error:
            raise RuntimeError(f"Relocated private protocol failed: {error}; stderr={errors!r}") from error
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=15)
            stdout_reader.join(timeout=2)
            stderr_reader.join(timeout=2)
            for pipe in (process.stdin, process.stdout, process.stderr):
                pipe.close()
    print("Relocated Pi initialize/shutdown passed with the packaged Node/SDK; no model request was made")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    args = parser.parse_args()
    smoke(args.prefix.absolute())
