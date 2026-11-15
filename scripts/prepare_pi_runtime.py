#!/usr/bin/env python3
"""Prepare the pinned Node/Pi production SDK closure as a relocatable prefix.

Fetching is an explicit build operation. --offline and --verify-stage never
download; no user npm configuration or dependency lifecycle scripts are used.
The P0 SDK stage is separate from the later product wrapper/UI integration.
"""
import argparse
import base64
import binascii
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from urllib.parse import unquote, urlparse
from urllib.request import urlopen
import zipfile


ROOT = Path(__file__).resolve().parents[1]
PI_VENDOR = ROOT / "vendor/pi"
NODE_LOCK = ROOT / "vendor/node/runtime.lock.json"
NODE_ROOT = Path("libexec/sicnu-geo-rs/node")
PI_ROOT = Path("libexec/sicnu-geo-rs/pi-agent")
AUX_ROOT = Path("share/sicnu-geo-rs/pi")
METADATA = Path("share/sicnu-geo-rs/pi-runtime.json")
PAYLOAD_ROOTS = (NODE_ROOT, PI_ROOT, AUX_ROOT)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_digest(path, expected):
    if not re.fullmatch(r"[0-9a-f]{64}", expected) or sha256(path) != expected:
        raise RuntimeError(f"SHA-256 mismatch: {path}")


def relative_path(value):
    path = PurePosixPath(value)
    if (not value or "\\" in value or ":" in value or path.is_absolute()
            or ".." in path.parts or "." in value.split("/")):
        raise RuntimeError(f"Unsafe relative path: {value}")
    return Path(*path.parts)


def native_target():
    machine = platform.machine().lower()
    if sys.platform == "win32" and machine in ("amd64", "x86_64"):
        return "x86_64-pc-windows-msvc"
    raise RuntimeError(f"No vendored Pi/Node distribution for {sys.platform}/{machine}")


def load_locks():
    pi = read_json(PI_VENDOR / "runtime.lock.json")
    node = read_json(NODE_LOCK)
    package = read_json(PI_VENDOR / "package.json")
    npm = read_json(PI_VENDOR / "package-lock.json")
    if pi.get("schema") != "sicnu.pi-runtime-lock/1" or node.get("schema") != "sicnu.node-runtime-lock/1":
        raise RuntimeError("Unsupported Pi/Node runtime lock schema")
    validate_npm_lock(npm, package, pi)
    return pi, node, package, npm


def valid_integrity(value):
    if not isinstance(value, str):
        return False
    algorithm, separator, encoded = value.partition("-")
    if not separator or algorithm not in ("sha256", "sha512"):
        return False
    try:
        return len(base64.b64decode(encoded, validate=True)) == {"sha256": 32, "sha512": 64}[algorithm]
    except (ValueError, binascii.Error):
        return False


def validate_npm_lock(lock, package, pi):
    if (lock.get("lockfileVersion") != 3 or lock.get("packages", {}).get("", {}).get("dependencies")
            != package.get("dependencies")):
        raise RuntimeError("Pi package-lock does not match its production package.json")
    packages = lock["packages"]
    for name, version in package["dependencies"].items():
        if packages.get("node_modules/" + name, {}).get("version") != version:
            raise RuntimeError(f"Pi direct dependency is not exactly pinned: {name}")
    for path, entry in packages.items():
        if not path:
            continue
        relative_path(path)
        if not path.startswith("node_modules/") or entry.get("link") or entry.get("dev"):
            raise RuntimeError(f"Pi lock contains a non-production package: {path}")
        parsed = urlparse(entry.get("resolved", ""))
        if (parsed.scheme != "https" or parsed.netloc != "registry.npmjs.org"
                or not valid_integrity(entry.get("integrity", ""))
                or not entry.get("version")):
            raise RuntimeError(f"Pi lock requires exact registry artifacts and integrity: {path}")
        if "/@earendil-works/pi-" in path and entry["version"] != pi["pi_version"]:
            raise RuntimeError(f"Pi packages must use one coherent release: {path}")
    sdk = packages.get("node_modules/" + pi["package"], {})
    if sdk.get("version") != pi["pi_version"] or sdk.get("integrity") != pi["package_integrity"]:
        raise RuntimeError("Pi SDK lock does not match the pinned upstream artifact")


def recipe_files(pi):
    manifest = ROOT / relative_path(pi["patch_manifest"])
    if not manifest.resolve().is_relative_to(PI_VENDOR.resolve()) or manifest.is_symlink():
        raise RuntimeError("Pi patch manifest leaves vendor/pi")
    files = [PI_VENDOR / name for name in ("runtime.lock.json", "package.json", "package-lock.json")]
    files.extend((NODE_LOCK, manifest))
    for patch in read_json(manifest).get("patches", []):
        for field in ("source_patch", "dist_patch"):
            if field not in patch:
                continue
            source = manifest.parent / relative_path(patch[field])
            if not source.resolve().is_relative_to(manifest.parent.resolve()) or source.is_symlink():
                raise RuntimeError("Pi patch leaves the patch directory")
            if field + "_sha256" in patch:
                require_digest(source, patch[field + "_sha256"])
            files.append(source)
    return files


def recipe_hashes(pi):
    return {path.relative_to(ROOT).as_posix(): sha256(path) for path in recipe_files(pi)}


def extract_node(archive, destination, artifact):
    expected = artifact["archive_root"]
    seen = set()
    with zipfile.ZipFile(archive) as source:
        for item in source.infolist():
            relative = relative_path(item.filename.rstrip("/"))
            if relative.parts[0] != expected or (item.external_attr >> 16) & 0o170000 == 0o120000:
                raise RuntimeError(f"Node archive entry leaves its root or is a symlink: {item.filename}")
            key = relative.as_posix().casefold()
            if key in seen:
                raise RuntimeError(f"Duplicate Node archive path: {item.filename}")
            seen.add(key)
        source.extractall(destination)
    unpacked = destination / expected
    for name in artifact["runtime_files"] + ["node_modules/npm/bin/npm-cli.js"]:
        if not (unpacked / relative_path(name)).is_file():
            raise RuntimeError(f"Required Node archive file is missing: {name}")
    return unpacked


def fetch_artifact(artifact, cache, offline):
    filename = unquote(Path(urlparse(artifact["url"]).path).name)
    if not filename or filename != Path(filename).name:
        raise RuntimeError("Invalid runtime archive URL filename")
    archive = cache / filename
    if archive.exists():
        require_digest(archive, artifact["sha256"])
        return archive
    if offline:
        raise RuntimeError(f"Offline cache is missing the pinned Node archive: {filename}")
    with tempfile.NamedTemporaryFile(dir=cache, prefix="node-download-", delete=False) as temporary:
        pending = Path(temporary.name)
        try:
            with urlopen(artifact["url"], timeout=60) as response:
                shutil.copyfileobj(response, temporary)
        except BaseException:
            temporary.close()
            pending.unlink(missing_ok=True)
            raise
    try:
        require_digest(pending, artifact["sha256"])
        pending.replace(archive)
    finally:
        pending.unlink(missing_ok=True)
    return archive


def process_environment(node_directory, state):
    # Do not inherit developer Node/npm injection or user credential variables.
    environment = {key: value for key, value in os.environ.items()
                   if key.upper() in {"SYSTEMROOT", "WINDIR", "SYSTEMDRIVE", "COMSPEC", "PATHEXT",
                                      "PROCESSOR_ARCHITECTURE", "NUMBER_OF_PROCESSORS"}}
    for directory in (state, state / "AppData/Roaming", state / "AppData/Local"):
        directory.mkdir(parents=True, exist_ok=True)
    environment.update({"PATH": str(node_directory) + os.pathsep + str(Path(os.environ.get("SYSTEMROOT", "C:/Windows")) / "System32"),
                        "USERPROFILE": str(state), "HOME": str(state), "APPDATA": str(state / "AppData/Roaming"),
                        "LOCALAPPDATA": str(state / "AppData/Local"), "TEMP": str(state), "TMP": str(state),
                        "PI_OFFLINE": "1", "PI_TELEMETRY": "0", "NPM_CONFIG_UPDATE_NOTIFIER": "false"})
    return environment


def checked_run(arguments, environment, cwd, timeout=1200):
    result = subprocess.run([str(value) for value in arguments], cwd=cwd, env=environment,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace", timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"Pi runtime preparation command failed ({result.returncode}):\n{result.stdout[-6000:]}")
    return result.stdout.strip()


def apply_patches(sdk_root, manifest, pi):
    patches = read_json(manifest)
    upstream = patches.get("upstream", {})
    if (patches.get("schema") != "sicnu.pi-vendor-patches/1"
            or upstream.get("version") != pi["pi_version"] or upstream.get("commit") != pi["source"]["commit"]
            or patches.get("package", pi.get("package")) != pi.get("package")):
        raise RuntimeError("Pi patches do not match the pinned upstream release")
    patched = []
    for patch in patches["patches"]:
        for entry in patch["files"]:
            target = sdk_root / relative_path(entry["path"])
            if target.is_symlink() or not target.resolve().is_relative_to(sdk_root.resolve()):
                raise RuntimeError("Pi patch target leaves the SDK root")
            require_digest(target, entry["before_sha256"])
            content = target.read_bytes()
            for replacement in entry["replacements"]:
                before = replacement["before"].encode("utf-8")
                after = replacement["after"].encode("utf-8")
                count = replacement.get("count", 1)
                if not before or count != 1 or content.count(before) != count:
                    raise RuntimeError(f"Pi patch does not match exactly once: {entry['path']}")
                content = content.replace(before, after, count)
            if hashlib.sha256(content).hexdigest() != entry["after_sha256"]:
                raise RuntimeError(f"Pi patched content digest differs: {entry['path']}")
            target.write_bytes(content)
            patched.append({"id": patch["id"], "path": entry["path"], "sha256": entry["after_sha256"]})
    if not patched:
        raise RuntimeError("Pi SDK requires its reviewed AuthContext isolation patch")
    return patched


def checked_status(path, status=None):
    status = path.lstat() if status is None else status
    if (stat.S_ISLNK(status.st_mode)
            or getattr(status, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)):
        raise RuntimeError(f"Pi runtime contains a symlink or reparse point: {path}")
    return status


def component_files(prefix, root):
    # Check component ancestors as well as descendants: libexec/ itself can be
    # a junction even when node/ has ordinary directory attributes.
    directory = prefix
    for part in root.parts:
        directory /= part
        try:
            status = checked_status(directory)
        except FileNotFoundError:
            return []
        if not stat.S_ISDIR(status.st_mode):
            raise RuntimeError(f"Pi runtime component root is not a directory: {directory}")
    files = []
    pending = [directory]
    while pending:
        directory = pending.pop()
        checked_status(directory)
        with os.scandir(directory) as children:
            for child in children:
                path = Path(child.path)
                status = checked_status(path, child.stat(follow_symlinks=False))
                if stat.S_ISDIR(status.st_mode):
                    pending.append(path)
                elif stat.S_ISREG(status.st_mode):
                    files.append(path)
                else:
                    raise RuntimeError(f"Pi runtime contains an unsupported file: {path}")
    return sorted(files)


def file_inventory(prefix, roots=PAYLOAD_ROOTS):
    try:
        status = checked_status(prefix)
    except FileNotFoundError:
        return []
    if not stat.S_ISDIR(status.st_mode):
        raise RuntimeError(f"Pi runtime prefix is not a directory: {prefix}")
    entries = []
    for root in roots:
        for path in component_files(prefix, root):
            entries.append({"path": path.relative_to(prefix).as_posix(), "sha256": sha256(path),
                            "size": path.stat().st_size})
    return entries


def verify_inventory(prefix, expected, actual=None):
    actual = file_inventory(prefix) if actual is None else actual
    if {entry["path"] for entry in actual} != {entry["path"] for entry in expected}:
        raise RuntimeError("Pi runtime file inventory differs from its receipt")
    if actual != expected:
        raise RuntimeError("Pi runtime file integrity differs from its receipt")


def installed_packages(sdk_root, lock):
    installed = []
    for path, artifact in lock["packages"].items():
        if not path:
            continue
        package_root = sdk_root / relative_path(path)
        if not package_root.exists() and artifact.get("optional"):
            continue
        metadata = read_json(package_root / "package.json")
        if metadata.get("version") != artifact["version"]:
            raise RuntimeError(f"Installed Pi dependency does not match the lock: {path}")
        notices = sorted(file.relative_to(sdk_root).as_posix() for file in package_root.iterdir()
                         if file.is_file() and re.match(r"(?i)^(license|notice|copying|third.party)", file.name))
        installed.append({"path": path, "name": metadata["name"], "version": metadata["version"],
                          "integrity": artifact["integrity"], "resolved": artifact["resolved"],
                          "license": metadata.get("license", metadata.get("licenses")), "notices": notices})
    return installed


def verify_stage(prefix):
    pi, node, package, npm = load_locks()
    target = native_target()
    # Refuse payload links before opening any receipt/package/recipe through
    # the caller's prefix. Reuse this ordered inventory below, not a second hash.
    actual_files = file_inventory(prefix)
    checked_status(prefix / METADATA)
    metadata = read_json(prefix / METADATA)
    if (metadata.get("schema") != "sicnu.pi-runtime/1" or metadata.get("target") != target
            or metadata.get("pi_version") != pi["pi_version"] or metadata.get("node_version") != node["node_version"]
            or metadata.get("node_source") != node["distributions"][target]
            or metadata.get("recipes") != recipe_hashes(pi)):
        raise RuntimeError("Pi runtime receipt does not match the pinned native recipe")
    for recipe in recipe_files(pi):
        bundled = prefix / AUX_ROOT / recipe.relative_to(ROOT)
        require_digest(bundled, sha256(recipe))
    if read_json(prefix / PI_ROOT / "package.json") != package or read_json(prefix / PI_ROOT / "package-lock.json") != npm:
        raise RuntimeError("Bundled Pi production package lock differs from the source")
    if not (prefix / NODE_ROOT / node["distributions"][target]["executable"]).is_file():
        raise RuntimeError("Bundled Node executable is missing")
    manifest = ROOT / relative_path(pi["patch_manifest"])
    for patch in read_json(manifest)["patches"]:
        for entry in patch["files"]:
            require_digest(prefix / PI_ROOT / "node_modules" / pi["package"] / relative_path(entry["path"]), entry["after_sha256"])
    if installed_packages(prefix / PI_ROOT, npm) != metadata["packages"]:
        raise RuntimeError("Pi production dependency closure differs from its receipt")
    verify_inventory(prefix, metadata["files"], actual_files)
    return metadata


def prepare(destination, cache, offline=False, node_archive=None):
    pi, node, package, npm = load_locks()
    target = native_target()
    artifact = node["distributions"][target]
    recipe_hashes(pi)  # Fail before downloads if the reviewed patch recipe is incomplete.
    if destination.exists():
        verify_stage(destination)
        print(f"Verified existing Pi {pi['pi_version']} stage: {destination}")
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    if node_archive is not None:
        archive = node_archive
        require_digest(archive, artifact["sha256"])
    else:
        archive = fetch_artifact(artifact, cache, offline)
    with tempfile.TemporaryDirectory(prefix=destination.name + "-prepare-", dir=destination.parent) as temporary:
        work = Path(temporary)
        if work.resolve().parent != destination.parent.resolve():
            raise RuntimeError("Preparation scratch escaped the destination parent")
        unpacked = extract_node(archive, work / "upstream-node", artifact)
        prefix = work / "prefix"
        runtime = prefix / NODE_ROOT
        runtime.mkdir(parents=True)
        for filename in artifact["runtime_files"]:
            shutil.copy2(unpacked / relative_path(filename), runtime / filename)
        sdk_root = prefix / PI_ROOT
        sdk_root.mkdir(parents=True)
        for filename in ("package.json", "package-lock.json"):
            shutil.copy2(PI_VENDOR / filename, sdk_root / filename)
        state = work / "build-state"
        environment = process_environment(unpacked, state)
        interpreter = unpacked / artifact["executable"]
        if checked_run([interpreter, "--version"], environment, work) != "v" + node["node_version"]:
            raise RuntimeError("Verified Node archive reports an unexpected version")
        user_config = state / "user.npmrc"
        global_config = state / "global.npmrc"
        user_config.write_text("", encoding="utf-8")
        global_config.write_text("", encoding="utf-8")
        npm_cli = unpacked / "node_modules/npm/bin/npm-cli.js"
        command = [interpreter, npm_cli, "ci", "--ignore-scripts", "--omit=dev", "--include=optional",
                   "--bin-links=false", "--no-audit", "--no-fund", "--engine-strict=true",
                   "--registry=https://registry.npmjs.org/", "--cache=" + str(cache / "npm"),
                   "--userconfig=" + str(user_config), "--globalconfig=" + str(global_config)]
        if offline:
            command.append("--offline")
        checked_run(command, environment, sdk_root)
        patched = apply_patches(sdk_root / "node_modules" / pi["package"], ROOT / relative_path(pi["patch_manifest"]), pi)
        packages = installed_packages(sdk_root, npm)
        for recipe in recipe_files(pi):
            output = prefix / AUX_ROOT / recipe.relative_to(ROOT)
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(recipe, output)
        write_json(prefix / AUX_ROOT / "third-party-packages.json", packages)
        metadata = {"schema": "sicnu.pi-runtime/1", "target": target, "pi_version": pi["pi_version"],
                    "node_version": node["node_version"], "node_source": artifact,
                    "recipes": recipe_hashes(pi), "patched_files": patched, "packages": packages,
                    "files": file_inventory(prefix)}
        write_json(prefix / METADATA, metadata)
        verify_stage(prefix)
        prefix.rename(destination)
    print(f"Prepared Pi {pi['pi_version']} / Node {node['node_version']}: {destination}")


def smoke(prefix):
    metadata = verify_stage(prefix)
    node = prefix / NODE_ROOT / "node.exe"
    with tempfile.TemporaryDirectory(prefix="sicnu-pi-smoke-") as temporary:
        environment = process_environment(node.parent, Path(temporary))
        output = checked_run([node, "--input-type=module", "-e", """
const sdk = await import('@earendil-works/pi-coding-agent');
const ai = await import('@earendil-works/pi-ai');
for (const name of ['createAgentSession', 'ModelRuntime', 'SettingsManager', 'SessionManager'])
  if (!sdk[name]) throw new Error('Missing Pi SDK export: ' + name);
for (const name of ['InMemoryCredentialStore', 'InMemoryModelsStore'])
  if (!ai[name]) throw new Error('Missing Pi AI export: ' + name);
console.log(JSON.stringify({node:process.version, sdkExports:true, aiExports:true, modelSessionCreated:false}));
"""], environment, prefix / PI_ROOT, timeout=120)
        print(output)
    print(f"Relocated Pi {metadata['pi_version']} receipt/public-export smoke passed; controlled-session/auth behavior was not exercised")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--destination", type=Path)
    action.add_argument("--verify-stage", type=Path)
    action.add_argument("--smoke", type=Path)
    parser.add_argument("--cache-dir", type=Path, default=ROOT / "build-logs/pi-vendor-cache")
    parser.add_argument("--node-archive", type=Path, help="already fetched official archive; its pinned digest is still required")
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    try:
        if args.destination:
            prepare(args.destination.absolute(), args.cache_dir.resolve(), args.offline,
                    args.node_archive.resolve() if args.node_archive else None)
        elif args.verify_stage:
            verify_stage(args.verify_stage.absolute())
            print("Vendored Pi/Node stage integrity passed")
        else:
            smoke(args.smoke.absolute())
    except (OSError, RuntimeError, ValueError, KeyError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        parser.exit(1, f"Pi runtime preparation failed: {error}\n")


if __name__ == "__main__":
    main()
