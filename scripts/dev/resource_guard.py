#!/usr/bin/env python3
"""resource_guard.py — cross-platform build lock and parallelism governor.

Wraps a build/test command so that

  * only ONE process at a time writes a given build directory (a second
    concurrent writer is refused with exit 75 EX_TEMPFAIL, or serialized
    behind the first when --wait is given),
  * build parallelism never exceeds the configured cap (default 2;
    `-j3+` is forbidden by the repository's resource policy): known
    command-line forms (`cmake --build … --parallel N` / `-j N`,
    `ninja -jN`, `make -jN`) are clamped downward, and the governing
    environment variables are exported for everything else.

The lock is an exclusively-created file at `<build-dir>.agent-build.lock`
(next to, never inside, the build tree). It records the holder's pid, host,
start time and command for diagnostics, and is only broken when the holder
pid is dead AND the lock is older than --stale-after (default 3600 s), so a
live build is never interrupted. Both the pid and the staleness clock are
host-local: a build directory shared over a network with another machine can
only ever be reported busy (fail-closed), never stolen.

Usage:
    python scripts/dev/resource_guard.py --lock-dir build-dev -- \\\\
        cmake --build build-dev --parallel 8
    python scripts/dev/resource_guard.py --lock-dir build-dev --wait 900 -- \\\\
        ctest --test-dir build-dev -j1 --output-on-failure
    python scripts/dev/resource_guard.py --lock-dir build-dev --status

Exit codes: the payload's own code on success, 75 busy (lock held, not
acquired), 2 usage refusal, 1 internal error.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev_common import EXIT_BUSY, EXIT_REFUSED, emit, pid_alive, run_streamed  # noqa: E402

LOCK_SUFFIX = ".agent-build.lock"
_PARALLEL_ENV = {"CMAKE_BUILD_PARALLEL_LEVEL", "CTEST_PARALLEL_LEVEL"}


def lock_path_for(build_dir: Path) -> Path:
    """Lock file keyed to the build directory (sibling file, never inside)."""
    return build_dir.parent / (build_dir.name + LOCK_SUFFIX)


def _norm(path: Path) -> str:
    resolved = Path(path).resolve()
    return os.path.normcase(str(resolved)) if os.name == "nt" else str(resolved)


def _read_holder(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def _write_holder(path: Path, payload: list[str]) -> dict:
    holder = {
        "pid": os.getpid(),
        "host": socket.gethostname(),
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "started_monotonic": time.monotonic(),
        "command": payload,
    }
    path.write_text(json.dumps(holder, indent=2), encoding="utf-8")
    return holder


def _is_stale(path: Path, stale_after: float) -> tuple[bool, dict]:
    holder = _read_holder(path)
    pid = holder.get("pid")
    if isinstance(pid, int) and pid_alive(pid):
        return False, holder
    started = holder.get("started_monotonic")
    if not isinstance(started, (int, float)):
        return False, holder
    age = time.monotonic() - started
    if age < stale_after:
        return False, holder
    return True, holder


def acquire(lock_path: Path, payload: list[str], wait: float, stale_after: float,
            verbose: bool) -> tuple[bool, dict]:
    """Try to take the lock until the wait budget runs out.

    Returns (acquired, info). Never breaks a lock whose recorded holder is
    still alive, and only breaks a dead holder's lock past the staleness age.
    """
    deadline = time.monotonic() + max(wait, 0.0)
    waited = 0.0
    while True:
        try:
            fd = os.open(str(lock_path), os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
        except FileExistsError:
            stale, holder = _is_stale(lock_path, stale_after)
            if stale:
                # Unlink-then-recreate is safe here: the recorded holder is
                # dead (pid liveness checked) and older than the staleness
                # threshold. A live holder's lock is never broken. The clock
                # and pid are host-local, so this only ever breaks locks from
                # THIS machine — a shared network build dir stays busy
                # (fail-closed) rather than being stolen.
                try:
                    lock_path.unlink()
                    continue
                except OSError:
                    # Someone else recreated it between our check and the
                    # unlink: fall through to the busy path below.
                    pass
            if time.monotonic() >= deadline:
                return False, {"reason": "busy", "holder": holder, "waited": round(waited, 3)}
            if verbose:
                print(f"resource-guard: build dir busy (holder pid "
                      f"{holder.get('pid')}, cmd {holder.get('command')}); waiting",
                      file=sys.stderr)
            time.sleep(0.5)
            waited += 0.5
            continue
        except OSError as exc:
            return False, {"reason": f"cannot create lock file: {exc}"}
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(json.dumps(_write_holder(lock_path, payload), indent=2))
        return True, {"waited": round(waited, 3)}


def release(lock_path: Path) -> None:
    try:
        lock_path.unlink()
    except FileNotFoundError:
        pass
    except OSError:
        pass


def clamp_parallelism(argv: list[str], cap: int) -> tuple[list[str], list[str]]:
    """Clamp known parallelism forms down to `cap`.

    Returns (new_argv, descriptions of what changed). Values already at or
    below the cap are left untouched (a deliberate -j1 pin survives).
    """
    out = list(argv)
    changes: list[str] = []

    def clamp_value(raw: str) -> tuple[str, bool]:
        try:
            value = int(raw)
        except ValueError:
            return raw, False
        if value > cap:
            return str(cap), True
        return raw, False

    i = 0
    while i < len(out):
        arg = out[i]
        if arg in ("--parallel", "-j") and i + 1 < len(out):
            raw = out[i + 1]
            new, changed = clamp_value(raw)
            if changed:
                out[i + 1] = new
                changes.append(f"{arg} {raw} -> {new}")
            i += 2
            continue
        if arg.startswith("--parallel="):
            new, changed = clamp_value(arg.split("=", 1)[1])
            if changed:
                changes.append(f"--parallel={arg.split('=',1)[1]} -> {new}")
                out[i] = f"--parallel={new}"
        elif arg.startswith("-j") and arg[2:].isdigit():
            new, changed = clamp_value(arg[2:])
            if changed:
                changes.append(f"-j{arg[2:]} -> -j{new}")
                out[i] = f"-j{new}"
        i += 1
    return out, changes


def governed_env(cap: int) -> dict[str, str]:
    env = dict(os.environ)
    env["CMAKE_BUILD_PARALLEL_LEVEL"] = str(cap)
    env["CTEST_PARALLEL_LEVEL"] = str(cap)
    if "MAKEFLAGS" not in env or "-j" not in env["MAKEFLAGS"]:
        env["MAKEFLAGS"] = f"-j{cap}"
    return env


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--lock-dir", required=True,
                        help="the build directory being written (lock keys on it)")
    parser.add_argument("--wait", type=float, default=0.0,
                        help="seconds to wait for a busy lock (0 = fail fast, default)")
    parser.add_argument("--parallel", type=int, default=2, choices=(1, 2),
                        help="parallelism cap for the wrapped build (default 2)")
    parser.add_argument("--stale-after", type=float, default=3600.0,
                        help="seconds after which a DEAD holder's lock may be broken")
    parser.add_argument("--status", action="store_true",
                        help="report the lock state for --lock-dir and exit")
    parser.add_argument("--json", action="store_true", help="machine-readable record")
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="the command to run, after --")
    args = parser.parse_args(argv)

    build_dir = Path(args.lock_dir)
    if not build_dir.is_absolute():
        build_dir = (Path.cwd() / build_dir).resolve()
    lock_path = lock_path_for(build_dir)

    if args.status:
        if lock_path.exists():
            holder = _read_holder(lock_path)
            alive = isinstance(holder.get("pid"), int) and pid_alive(holder["pid"])
            payload = {"lock": str(lock_path), "held": True,
                       "holder": holder, "holder_alive": alive}
        else:
            payload = {"lock": str(lock_path), "held": False}
        emit(payload, args.json, [f"{payload['lock']}: "
                                  f"{'held' if payload['held'] else 'free'}"])
        return 0

    payload_cmd = list(args.command)
    if payload_cmd and payload_cmd[0] == "--":
        payload_cmd = payload_cmd[1:]
    if not payload_cmd:
        print("refused: no command given (use -- <command>)", file=sys.stderr)
        return EXIT_REFUSED

    payload_cmd, changes = clamp_parallelism(payload_cmd, args.parallel)
    if changes:
        print(f"resource-guard: clamped parallelism: {'; '.join(changes)}",
              file=sys.stderr)

    acquired, info = acquire(lock_path, payload_cmd, args.wait, args.stale_after,
                             verbose=not args.json)
    if not acquired:
        record = {"acquired": False, "lock": str(lock_path), "detail": info,
                  "exit_code": EXIT_BUSY}
        if args.json:
            print(json.dumps(record, indent=2))
        else:
            print(f"refused: build dir {build_dir} is locked by pid "
                  f"{info.get('holder', {}).get('pid')} (waited {info.get('waited')}s); "
                  f"another build is writing it", file=sys.stderr)
        return EXIT_BUSY

    try:
        proc = run_streamed(payload_cmd, env=governed_env(args.parallel))
        record = {"acquired": True, "lock": str(lock_path),
                  "lock_dir": str(build_dir), "command": payload_cmd,
                  "parallel_cap": args.parallel, "waited_for_lock": info.get("waited"),
                  "payload_exit_code": proc.returncode}
        if args.json:
            print(json.dumps(record, indent=2))
        return proc.returncode
    finally:
        release(lock_path)


if __name__ == "__main__":
    sys.exit(main())
