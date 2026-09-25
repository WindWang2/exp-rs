#!/usr/bin/env python3
"""narrow_targets.py — changed-path → minimal build/test target evidence.

Reads the repository's CMake wiring (target definitions, target_sources and
the sicnu_* test helper registrations) and maps changed paths to the narrowest
targets that compile/exercise them, so incremental verification can build a
handful of object/test targets instead of the default `all`.

The mapping is mechanical — no hand-maintained path table, no second truth
source: a target covers a path only when the wiring text names it. Anything
the wiring does not reach is reported as `unwired`, never guessed into a
target.

Mapping order per changed path:
  1. exact source-list match — a target lists the path (root-relative, or via
     ${CMAKE_SOURCE_DIR}/${CMAKE_CURRENT_SOURCE_DIR}) as one of its sources;
  2. test registration — the path is tests/<stem>.cpp of a helper-registered
     test target (sicnu_add_test and siblings): build + ctest that target;
  3. CMake script itself — for a changed CMakeLists.txt/*.cmake: the targets
     defined in that script plus the build-wiring drift oracle
     (`test_build_wiring_drift`), which guards the wiring text;
  4. basename fallback — targets listing a same-named source elsewhere
     (reported as `weak`: could shadow or be shadowed by a sibling module);
  5. otherwise the path is `unwired` — no narrow target exists; escalate to a
     configure-level check instead of inventing one.

Usage:
    python scripts/dev/narrow_targets.py src/foo/x.cpp tests/test_x.cpp
    python scripts/dev/narrow_targets.py --base origin/master
    python scripts/dev/narrow_targets.py ... --json [--build-dir build]

Exit codes: 0 mapped (possibly partially — check `unwired`/`weak` in the
payload), 1 genuine failure, 2 refused (not a repo / no paths / bad base).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev_common import (  # noqa: E402
    EXIT_FAILURE, EXIT_OK, EXIT_REFUSED, emit, run_git,
)

TARGET_CMDS = {"add_executable", "add_library", "qt_add_executable", "qt_add_library", "qt_add_plugin"}
SOURCE_EXTS = {".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hh", ".ui", ".qrc", ".mm", ".m", ".rc"}
WIRING_ORACLE_TARGET = "test_build_wiring_drift"
# presentation cap: scripts defining more targets than this contribute the
# count (plus the wiring oracle) instead of the full list
_MAX_SCRIPT_TARGETS = 20
# scoped-token keywords that appear inside target command argument lists
_SOURCE_NOISE = {
    "STATIC", "SHARED", "MODULE", "INTERFACE", "OBJECT", "UNKNOWN", "IMPORTED",
    "ALIAS", "GLOBAL", "EXCLUDE_FROM_ALL", "WIN32", "MACOSX_BUNDLE", "PRIVATE",
    "PUBLIC", "HEADERS", "SOURCES",
}


def _refuse(message: str) -> int:
    print(message, file=sys.stderr)
    return EXIT_REFUSED


def strip_comments(text: str) -> str:
    """Drop '#'..end-of-line so commented wiring stops counting."""
    out: list[str] = []
    for line in text.splitlines():
        quoted = False
        kept: list[str] = []
        for ch in line:
            if ch == '"':
                quoted = not quoted
            if ch == "#" and not quoted:
                break
            kept.append(ch)
        out.append("".join(kept))
    return "\n".join(out)


def scan_commands(text: str) -> list[tuple[str, str]]:
    """(name, args) for every command invocation, balanced-paren scanned."""
    out: list[tuple[str, str]] = []
    i = 0
    n = len(text)
    while i < n:
        if not (text[i].isalpha() or text[i] == "_"):
            i += 1
            continue
        start = i
        while i < n and (text[i].isalnum() or text[i] == "_"):
            i += 1
        name = text[start:i]
        j = i
        while j < n and text[j].isspace():
            j += 1
        if j >= n or text[j] != "(":
            continue
        depth = 1
        k = j + 1
        while k < n and depth:
            if text[k] == "(":
                depth += 1
            elif text[k] == ")":
                depth -= 1
            k += 1
        out.append((name, text[j + 1:k - 1]))
        i = k
    return out


def resolve_source_token(token: str, script_dir: Path, repo_root: Path) -> tuple[str, bool]:
    """Normalize one source token to a root-relative path when possible.

    Returns (path, strong). Strong = fully resolvable against the repo root;
    weak = the token references an unresolvable variable, so only its basename
    is known.
    """
    token = token.strip('"')
    if "$" in token:
        replaced = token
        replaced = replaced.replace("${CMAKE_SOURCE_DIR}", str(repo_root))
        replaced = replaced.replace("${SICNU_SOURCE_DIR}", str(repo_root))
        replaced = replaced.replace("${PROJECT_SOURCE_DIR}", str(repo_root))
        replaced = replaced.replace("${CMAKE_CURRENT_SOURCE_DIR}", str(script_dir))
        if "$" in replaced:
            return (Path(token).name, False)
        token = replaced
    p = Path(token)
    if not p.is_absolute():
        p = script_dir / p
    try:
        rel = p.resolve().relative_to(repo_root.resolve())
    except ValueError:
        return (p.name, False)
    return (rel.as_posix(), True)


def _first_token(args: str) -> str:
    parts = args.split()
    return parts[0] if parts else ""


def _is_target_name(token: str) -> bool:
    return bool(re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.:-]*", token or ""))


def _ends_with_source_ext(token: str) -> bool:
    return Path(token).suffix in SOURCE_EXTS


class Wiring:
    def __init__(self, repo_root: Path) -> None:
        self.repo_root = repo_root
        self.targets: dict[str, dict] = {}
        self.helpers: set[str] = set()
        self.scripts: list[Path] = []
        # link graph: target -> [(name, propagates)] where propagates marks
        # PUBLIC/INTERFACE edges (PRIVATE on SHARED libs does not reach
        # consumers; the wiring text cannot always tell the library type, so
        # PRIVATE is kept but flagged)
        self.links: dict[str, list[tuple[str, str]]] = {}
        self.alias_of: dict[str, str] = {}
        # targets registered with the ctest harness (sicnu_discover_tests /
        # catch_discover_tests / add_test) — the only ones ctest can run
        self.ctest_registered: set[str] = set()
        self._scan()

    def _cmake_scripts(self) -> list[Path]:
        exclude = {".git", "CMakeFiles", "Testing"}
        out: list[Path] = []
        for dirpath, dirnames, filenames in self.repo_root.walk():
            dirnames[:] = sorted(d for d in dirnames
                                 if d not in exclude
                                 and not (d == "build" or d.startswith("build-")))
            for f in filenames:
                if f == "CMakeLists.txt" or f.endswith(".cmake"):
                    out.append(Path(dirpath) / f)
        return sorted(out)

    def _scan(self) -> None:
        self.scripts = self._cmake_scripts()
        stripped: dict[Path, str] = {}
        for s in self.scripts:
            try:
                stripped[s] = strip_comments(s.read_text(encoding="utf-8", errors="replace"))
            except OSError:
                stripped[s] = ""

        # helpers: functions/macros that create a target from a ${PARAM} arg
        # and (optionally) reference the implicit ${PARAM}.cpp source.
        for s in self.scripts:
            text = stripped[s]
            for name, args in scan_commands(text):
                if name not in ("function", "macro"):
                    continue
                params = args.split()
                if len(params) < 2:
                    continue
                fname, pnames = params[0], set(params[1:])
                endkw = "end" + name
                start = text.find(f"{name}(", 0)
                while start != -1:
                    body_at = text.find(")", start) + 1
                    end = text.find(f"{endkw}(", body_at)
                    if end == -1:
                        break
                    body = text[body_at:end]
                    for tcmd in TARGET_CMDS:
                        m = re.search(re.escape(tcmd) + r"\s*\(\s*\$\{(\w+)\}", body)
                        if m and m.group(1) in pnames:
                            self.helpers.add(fname)
                            break
                    start = text.find(f"{name}(", end)

        # per-script literal set()/list(APPEND) variables — ${…}-composed
        # tokens are kept: the chain resolution below expands the resolvable
        # ones (${CMAKE_SOURCE_DIR}/x, references to earlier variables)
        script_vars: dict[Path, dict[str, list[str]]] = {}
        for s in self.scripts:
            try:
                text = stripped[s]
            except KeyError:
                text = strip_comments(s.read_text(encoding="utf-8", errors="replace"))
                stripped[s] = text
            for name, args in scan_commands(text):
                toks = args.split()
                if name == "set" and len(toks) >= 2 \
                        and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", toks[0]):
                    script_vars.setdefault(s, {}).setdefault(toks[0], []).extend(toks[1:])
                elif name == "list" and len(toks) >= 3 and toks[0] == "APPEND" \
                        and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", toks[1]):
                    script_vars.setdefault(s, {}).setdefault(toks[1], []).extend(toks[2:])
        # fixed-point chain resolution, depth-capped
        for sv in script_vars.values():
            for _ in range(8):
                changed = False
                for var, tokens in sv.items():
                    nxt: list[str] = []
                    for tok in tokens:
                        if "$" in tok:
                            m = re.fullmatch(r"\$\{(\w+)\}", tok)
                            if m and m.group(1) in sv and m.group(1) != var:
                                nxt.extend(sv[m.group(1)])
                                changed = True
                                continue
                        nxt.append(tok)
                    if nxt != tokens:
                        sv[var] = nxt
                if not changed:
                    break

        # target definitions and their source lists. Sources composed through
        # set()/list(APPEND) variables are expanded per script, in command
        # order — the QGIS-style `set(QGIS_CORE_SRCS a.cpp …)` convention.

        def expand(tok: str, script: Path) -> list[str]:
            m = re.fullmatch(r"\$\{(\w+)\}", tok)
            if m:
                return list(script_vars.get(script, {}).get(m.group(1), []))
            return [tok]

        def harvest(entry: dict, toks: list[str], script: Path) -> None:
            for tok in toks:
                for part in expand(tok, script):
                    if part in _SOURCE_NOISE or not _ends_with_source_ext(part):
                        continue
                    entry["sources"].append(resolve_source_token(part, script.parent,
                                                                 self.repo_root))

        for s in self.scripts:
            text = stripped[s]
            for name, args in scan_commands(text):
                if name in TARGET_CMDS:
                    toks = args.split()
                    if not toks or not _is_target_name(toks[0]) or _ends_with_source_ext(toks[0]):
                        continue
                    if len(toks) >= 3 and name == "add_library" and toks[1] == "ALIAS":
                        self.alias_of[toks[0]] = toks[2]
                        continue
                    entry = self.targets.setdefault(
                        toks[0], {"script": s, "sources": [], "implicit": False,
                                  "kind": "library" if name == "add_library" else "executable"})
                    harvest(entry, toks[1:], s)
                elif name == "target_sources":
                    toks = args.split()
                    if not toks or not _is_target_name(toks[0]):
                        continue
                    entry = self.targets.setdefault(
                        toks[0], {"script": s, "sources": [], "implicit": False})
                    harvest(entry, toks[1:], s)
                elif name in ("sicnu_discover_tests", "catch_discover_tests", "add_test",
                              "gtest_discover_tests", "catch_discover_tests"):
                    tname = _first_token(args)
                    if _is_target_name(tname) and "$" not in tname:
                        self.ctest_registered.add(tname)
                elif name in ("target_link_libraries", "link_libraries"):
                    toks = args.split()
                    scope = ""
                    subject = ""
                    items = toks
                    if name == "target_link_libraries":
                        if not items or not _is_target_name(items[0]) or "$" in items[0]:
                            continue
                        if _ends_with_source_ext(items[0]) or items[0] in _SOURCE_NOISE:
                            continue
                        subject = items[0]
                        items = items[1:]
                    for tok in items:
                        if tok in ("PUBLIC", "INTERFACE", "PRIVATE"):
                            scope = tok
                            continue
                        if tok in ("debug", "optimized", "general", "LINK_PUBLIC",
                                   "LINK_PRIVATE", "LINK_INTERFACE_LIBRARIES"):
                            continue
                        if tok.startswith("-") or "$" in tok:
                            continue
                        # scanner desyncs (paren inside a quoted string) can
                        # glue neighbouring commands in: build-type keywords
                        # and source files are never link items
                        if tok in _SOURCE_NOISE or _ends_with_source_ext(tok):
                            continue
                        propagates = "public" if scope in ("PUBLIC", "INTERFACE", "") else "private"
                        if subject:
                            self.links.setdefault(subject, []).append((tok, propagates))
                        else:
                            # directory-scope link_libraries(): every target
                            # this script defines inherits the dependency
                            for tname, entry in self.targets.items():
                                if entry["script"] == s:
                                    self.links.setdefault(tname, []).append((tok, propagates))
                elif name in self.helpers:
                    tname = _first_token(args)
                    if not _is_target_name(tname):
                        continue
                    entry = self.targets.setdefault(
                        tname, {"script": s, "sources": [], "implicit": True,
                                "kind": "executable"})
                    if not entry["sources"]:
                        # helper template: the implicit ${NAME}.cpp next to the
                        # calling script (tests/CMakeLists.txt convention)
                        entry["sources"].append(((s.parent / (tname + ".cpp")).resolve()
                                                 .relative_to(self.repo_root.resolve()).as_posix(),
                                                 True))
                        entry["implicit"] = True
                    # keyword groups at the call site (SOURCES/LIBS style):
                    # values of *_SOURCES-style groups are embed sources of
                    # this target (d17 test family convention)
                    rest = args.split()[1:]
                    current_kw = ""
                    for tok in rest:
                        if re.fullmatch(r"[A-Z][A-Z0-9_]*", tok) and "$" not in tok:
                            current_kw = tok
                            continue
                        if "SOURCE" in current_kw:
                            # ${VAR}-composed values expand through the
                            # script's resolved variable lists
                            harvest(entry, expand(tok, s), s)

    def targets_defining_script(self, script: Path) -> list[str]:
        want = script.resolve()
        return sorted(t for t, e in self.targets.items() if e["script"].resolve() == want)


def collect_paths(repo_root: Path, args: argparse.Namespace) -> list[str] | None:
    if args.paths:
        return list(args.paths)
    if args.base:
        proc = run_git(repo_root, ["diff", "--name-only", f"{args.base}...HEAD"])
        if proc.returncode != 0:
            return None
        paths = [l for l in proc.stdout.splitlines() if l.strip()]
        proc = run_git(repo_root, ["diff", "HEAD", "--name-only"])
        if proc.returncode == 0:
            paths += [l for l in proc.stdout.splitlines() if l.strip()]
        return sorted(set(paths))
    return None


def _resolve_alias(wiring: Wiring, name: str) -> str:
    seen = set()
    while name in wiring.alias_of and name not in seen:
        seen.add(name)
        name = wiring.alias_of[name]
    return name


def _consumers(wiring: Wiring, owners: set[str]) -> dict[str, list[str]]:
    """Executables/test targets that (transitively) link any of `owners`.

    Follows every link edge — for STATIC libraries even PRIVATE dependencies
    propagate (LINK_ONLY), and the wiring text cannot always tell the library
    type, so this is deliberately the over-approximation: it may suggest a
    consumer that would not actually relink, never hide one.
    """
    root_targets = {_resolve_alias(wiring, o) for o in owners}
    consumers: dict[str, list[str]] = {}
    for tname, entry in wiring.targets.items():
        kind = entry.get("kind", "")
        if kind != "executable":
            continue
        seen: set[str] = set()
        stack = [tname]
        hit: list[str] = []
        while stack:
            cur = stack.pop()
            real = _resolve_alias(wiring, cur)
            if real in seen:
                continue
            seen.add(real)
            if real in root_targets and real != tname:
                hit.append(real)
            for lib, _scope in wiring.links.get(real, []):
                r = _resolve_alias(wiring, lib)
                if r not in seen and (r in wiring.targets or r in wiring.alias_of):
                    stack.append(r)
        if hit:
            consumers[tname] = sorted(hit)
    return consumers


def map_paths(wiring: Wiring, paths: list[str]) -> dict:
    build: dict[str, dict] = {}
    ctest: list[str] = []
    unwired: list[str] = []
    huge_scripts: dict[str, int] = {}
    by_rel: dict[str, list[str]] = {}
    by_basename: dict[str, list[str]] = {}
    for tname, entry in wiring.targets.items():
        for src, strong in entry["sources"]:
            if strong:
                by_rel.setdefault(src, []).append(tname)
            else:
                by_basename.setdefault(Path(src).name, []).append(tname)

    for raw in paths:
        rel = raw
        strong_hits = list(by_rel.get(rel, []))
        # last resort for variable-resolved source lists: same-named source
        # elsewhere — reported as weak because a basename cannot disambiguate
        # sibling modules
        weak_hits = [t for t in by_basename.get(Path(rel).name, []) if t not in strong_hits]
        hits: list[str] = list(strong_hits)
        script_hit = raw.endswith("CMakeLists.txt") or raw.endswith(".cmake")
        if script_hit:
            p = wiring.repo_root / raw
            defined = wiring.targets_defining_script(p)
            # A wiring change genuinely affects every target the script
            # defines; dumping hundreds of names (tests/CMakeLists.txt) is
            # not a usable suggestion, so oversized scripts contribute the
            # count plus the wiring oracle instead of an unusable list.
            if len(defined) > _MAX_SCRIPT_TARGETS:
                huge_scripts[raw] = len(defined)
            else:
                hits += [t for t in defined if t not in hits]
            if WIRING_ORACLE_TARGET not in hits:
                hits.append(WIRING_ORACLE_TARGET)
        if not hits:
            hits += weak_hits
        if hits:
            for t in sorted(set(hits)):
                weak = t not in strong_hits and not script_hit
                info = build.setdefault(t, {"weak": weak, "paths": []})
                info["paths"].append(raw)
                if weak is False:
                    info["weak"] = False
            stem = Path(raw).stem
            entry = wiring.targets.get(stem)
            if (entry and entry.get("implicit") and strong_hits
                    and any(src == raw for src, _ in entry["sources"])):
                ctest.append(stem)
        else:
            unwired.append(raw)

    # classification + verification level per suggested target
    details: dict[str, dict] = {}
    for t, v in sorted(build.items()):
        entry = wiring.targets.get(t, {})
        kind = entry.get("kind", "unknown")
        if kind == "executable" or t in ctest:
            verification = "link+test"
        elif kind == "library":
            verification = "compile+link"
        else:
            verification = "compile"
        details[t] = {"weak": v["weak"], "paths": sorted(set(v["paths"])),
                      "kind": kind, "verification": verification}

    # consumers: test executables that link the changed code (transitively)
    owners = {t for t, d in details.items() if d["kind"] == "library" and not d["weak"]}
    consumers = _consumers(wiring, owners) if owners else {}
    # the consuming executables are themselves build+test verification for
    # the changed sources; fold the new ones into the suggestion set
    for consumer in consumers:
        if consumer not in details:
            details[consumer] = {"weak": False,
                                 "paths": sorted({p for hit in consumers[consumer]
                                                  for p in paths
                                                  if hit in {t for t, d in details.items()
                                                             if p in d["paths"]}}),
                                 "kind": "executable",
                                 "verification": "link+test",
                                 "reason": "links " + ", ".join(consumers[consumer])}
        if consumer in wiring.ctest_registered:
            ctest.append(consumer)

    return {
        "build_targets": sorted(details),
        "target_details": details,
        "consumers": {c: {"exercises": hits, "verification": "link+test"}
                      for c, hits in sorted(consumers.items())},
        "ctest_targets": sorted(set(ctest)),
        "ctest_suggestion": (
            "ctest --test-dir <build> -R '^(" + "|".join(re.escape(t) for t in sorted(set(ctest))) + ")'"
            if ctest else None),
        "oversized_scripts": huge_scripts,
        "unwired": sorted(set(unwired)),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="*", help="changed paths to map")
    parser.add_argument("--base", metavar="BASE",
                        help="git base; map committed diff BASE...HEAD plus tracked "
                             "worktree changes (staged+unstaged; untracked files excluded)")
    parser.add_argument("--json", action="store_true", help="machine-readable payload")
    ns = parser.parse_args(argv)

    proc = run_git(Path("."), ["rev-parse", "--show-toplevel"])
    if proc.returncode != 0:
        return _refuse(f"not a git repository: {proc.stderr.strip()}")
    repo_root = Path(proc.stdout.strip())

    paths = collect_paths(repo_root, ns)
    if paths is None:
        return _refuse("no paths to map: pass paths or a resolvable --base")
    if not paths:
        payload = {"build_targets": [], "target_details": {}, "ctest_targets": [],
                   "ctest_suggestion": None, "oversized_scripts": {}, "unwired": []}
        emit(payload, ns.json, ["no changed paths; nothing to map"])
        return EXIT_OK

    # normalize every path against the repo root: spellings like ./x or
    # ../repo/x must map identically, and paths escaping the repo are refused
    # rather than silently unmapped
    root_resolved = repo_root.resolve()
    normalized: list[str] = []
    for raw in paths:
        p = Path(raw)
        if not p.is_absolute():
            p = repo_root / p
        try:
            normalized.append(p.resolve().relative_to(root_resolved).as_posix())
        except ValueError:
            return _refuse(f"path outside the repository: {raw}")
    paths = normalized

    try:
        wiring = Wiring(repo_root)
        payload = map_paths(wiring, paths)
    except Exception as exc:  # noqa: BLE001 — a broken mapping is a genuine failure
        print(f"narrow_targets: mapping failed: {exc}", file=sys.stderr)
        return EXIT_FAILURE

    lines = []
    if payload["build_targets"]:
        lines.append("narrow build targets: " + " ".join(payload["build_targets"]))
    if payload["ctest_suggestion"]:
        lines.append(payload["ctest_suggestion"])
    if payload["unwired"]:
        lines.append("unwired (no narrow target; escalate to configure-level check): "
                     + " ".join(payload["unwired"]))
    for script, count in payload["oversized_scripts"].items():
        lines.append(f"oversized wiring script (defines {count} targets): {script} "
                     f"-> {WIRING_ORACLE_TARGET} only")
    if not lines:
        lines.append("no changed paths; nothing to map")
    emit(payload, ns.json, lines)
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
