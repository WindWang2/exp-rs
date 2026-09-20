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
import errno
import json
import os
import re
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev_common import (  # noqa: E402
    EXIT_BUSY, EXIT_FAILURE, EXIT_REFUSED, emit, pid_alive, run_streamed,
)

LOCK_SUFFIX = ".agent-build.lock"
_PARALLEL_ENV = {"CMAKE_BUILD_PARALLEL_LEVEL", "CTEST_PARALLEL_LEVEL"}


def lock_path_for(build_dir: Path) -> Path:
    """Lock file keyed to the build directory (sibling file, never inside).

    The build dir is canonicalised with realpath so that two spellings of one
    directory (relative vs absolute, case differences on Windows, a junction
    or symlink to the same tree) resolve to ONE lock file.
    """
    real = Path(os.path.realpath(str(build_dir)))
    return real.parent / (real.name + LOCK_SUFFIX)


def _read_holder(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, ValueError):
        return {}


def _holder_record(payload: list[str]) -> dict:
    return {
        "pid": os.getpid(),
        "host": socket.gethostname(),
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "started_monotonic": time.monotonic(),
        "command": payload,
    }


def _is_stale(path: Path, stale_after: float) -> tuple[bool, dict]:
    """Decide whether an existing lock may be broken.

    Both gates must say "dead": the recorded holder pid (host-local) must not
    be alive, and the lock must be older than `stale_after`. Age comes from
    the holder record when it is readable, and from the file's own mtime
    otherwise — an empty or corrupt lock (e.g. the holder was SIGKILLed in the
    window between creating the file and writing its record) must still be
    breakable once old enough, or nobody could ever build here again.
    """
    holder = _read_holder(path)
    pid = holder.get("pid")
    if isinstance(pid, int) and pid_alive(pid):
        return False, holder
    started = holder.get("started_monotonic")
    if isinstance(started, (int, float)):
        age = time.monotonic() - started
    else:
        try:
            age = time.time() - path.stat().st_mtime
        except OSError:
            return False, holder
    if age < stale_after:
        return False, holder
    return True, holder


def _break_if_still_stale(lock_path: Path, before: bytes, stale_after: float
                          ) -> bool:
    """Unlink a stale lock ONLY if it still holds the exact bytes we judged.

    Re-reading before the unlink closes the TOCTOU window in which another
    process replaces the lock with a LIVE holder's record: if the bytes
    changed, the lock is no longer ours to break and we report busy instead.
    """
    try:
        current = lock_path.read_bytes()
    except FileNotFoundError:
        return True  # already gone; the next acquisition attempt will create it
    except OSError:
        return False
    if current != before:
        return False
    stale, _holder = _is_stale(lock_path, stale_after)
    if not stale:
        return False
    try:
        lock_path.unlink()
    except OSError:
        return False
    return True


def acquire(lock_path: Path, payload: list[str], wait: float, stale_after: float,
            verbose: bool) -> tuple[bool, dict]:
    """Try to take the lock until the wait budget runs out.

    The lock file is created atomically WITH its holder record (write to a
    temp file, then os.link into place) so there is no window in which the
    lock exists but is empty/unparseable. Never breaks a lock whose recorded
    holder is alive, and only breaks a dead holder's lock past the staleness
    age, re-verifying the bytes immediately before the unlink.
    """
    deadline = time.monotonic() + max(wait, 0.0)
    waited = 0.0
    tried_mkdir = False
    while True:
        holder = _holder_record(payload)
        staged = lock_path.parent / (lock_path.name + f".{os.getpid()}.staged")
        try:
            # 0o600: the lock file records the build command; on a shared
            # multi-user build host it should not be world-readable.
            fd = os.open(str(staged), os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            try:
                os.write(fd, json.dumps(holder, indent=2).encode("utf-8"))
            finally:
                os.close(fd)
            os.link(str(staged), str(lock_path))
            staged.unlink()
            return True, {"waited": round(waited, 3)}
        except FileExistsError:
            try:
                staged.unlink()
            except OSError:
                pass
            stale, holder = _is_stale(lock_path, stale_after)
            if stale:
                try:
                    before = lock_path.read_bytes()
                except OSError:
                    before = b""
                if _break_if_still_stale(lock_path, before, stale_after):
                    continue
                stale = False
            if time.monotonic() >= deadline:
                return False, {"reason": "busy", "holder": holder,
                               "waited": round(waited, 3)}
            if verbose:
                print(f"resource-guard: build dir busy (holder pid "
                      f"{holder.get('pid')}, cmd {holder.get('command')}); waiting",
                      file=sys.stderr)
            time.sleep(0.5)
            waited += 0.5
            continue
        except OSError as exc:
            try:
                staged.unlink()
            except OSError:
                pass
            if exc.errno == errno.ENOENT and not tried_mkdir:
                # The build directory's parent does not exist yet (the build
                # tree was never configured): create it once. A missing
                # directory is not a lock conflict, so it must never surface
                # as "busy".
                tried_mkdir = True
                try:
                    lock_path.parent.mkdir(parents=True, exist_ok=True)
                    continue
                except OSError:
                    pass
            return False, {"reason": f"cannot create lock file: {exc.errno} {exc}"}


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
    """Environment for the payload with the parallelism cap applied.

    An inherited `MAKEFLAGS=-jN` would otherwise defeat the cap for `make`,
    so every `-j<digits>` token is stripped before the cap is appended.
    """
    env = dict(os.environ)
    env["CMAKE_BUILD_PARALLEL_LEVEL"] = str(cap)
    env["CTEST_PARALLEL_LEVEL"] = str(cap)
    makeflags = env.get("MAKEFLAGS", "")
    tokens = [t for t in makeflags.split() if not re.fullmatch(r"-j\d*", t)]
    tokens.append(f"-j{cap}")
    env["MAKEFLAGS"] = " ".join(tokens)
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
        reason = info.get("reason", "busy")
        if reason == "busy":
            message = (f"refused: build dir {build_dir} is locked by pid "
                       f"{info.get('holder', {}).get('pid')} "
                       f"(waited {info.get('waited')}s); another build is writing it")
            record = {"acquired": False, "lock": str(lock_path),
                      "detail": info, "exit_code": EXIT_BUSY}
            code = EXIT_BUSY
        else:
            # lock-creation failure (permissions, EMFILE, …) is a hard
            # failure, not contention — never reported as "busy"
            message = (f"refused: cannot create lock file {lock_path} for build dir "
                       f"{build_dir}: {reason}")
            record = {"acquired": False, "lock": str(lock_path),
                      "detail": info, "exit_code": EXIT_FAILURE}
            code = EXIT_FAILURE
        if args.json:
            print(json.dumps(record, indent=2))
        else:
            print(message, file=sys.stderr)
        return code

    try:
        try:
            proc = run_streamed(payload_cmd, env=governed_env(args.parallel))
        except FileNotFoundError:
            print(f"refused: payload command not found: {payload_cmd[0]}",
                  file=sys.stderr)
            return EXIT_FAILURE
        except KeyboardInterrupt:
            print("resource-guard: interrupted; lock released", file=sys.stderr)
            return 130
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
