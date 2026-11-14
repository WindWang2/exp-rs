#!/usr/bin/env python3
"""Stage the pinned, relocatable Python runtime and offline geospatial wheels.

Downloads are explicit build operations. Application startup and CMake configure
never fetch Python. The destination is a fresh install-prefix tree, not a venv.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from urllib.parse import unquote, urlparse
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "vendor/python/runtime.lock.json"
REQUIREMENTS = ROOT / "data/python/geospatial-v1/requirements.lock"
METADATA = Path("share/sicnu-geo-rs/python-runtime.json")
PYTHON_ROOT = Path("libexec/sicnu-geo-rs/python")
WHEELHOUSE = Path("share/sicnu-geo-rs/python-wheels/geospatial-v1")


def load_lock():
    return json.loads(LOCK_PATH.read_text(encoding="utf-8"))


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_digest(path, expected):
    if sha256(path) != expected:
        raise RuntimeError(f"SHA-256 mismatch: {path}")


def native_target():
    machine = platform.machine().lower()
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64",
            "aarch64": "aarch64"}.get(machine, machine)
    suffix = {"win32": "pc-windows-msvc", "linux": "unknown-linux-gnu",
              "darwin": "apple-darwin"}.get(sys.platform)
    target = f"{arch}-{suffix}"
    if suffix is None or target not in load_lock()["distributions"]:
        raise RuntimeError(f"No vendored Python distribution for {sys.platform}/{machine}")
    return target


def executable(prefix, target):
    return prefix / PYTHON_ROOT / ("python.exe" if "windows" in target else "bin/python3")


def checked_run(arguments):
    environment = os.environ.copy()
    for key in ("PYTHONHOME", "PYTHONPATH", "PYTHONUSERBASE", "PYTHONSTARTUP", "__PYVENV_LAUNCHER__"):
        environment.pop(key, None)
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    result = subprocess.run([str(argument) for argument in arguments], env=environment,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", timeout=1200)
    if result.returncode:
        raise RuntimeError(f"Python preparation command failed ({result.returncode}):\n{result.stdout[-8000:]}")
    return result.stdout.strip()


def extract_runtime(archive, destination):
    if not hasattr(tarfile, "data_filter"):
        raise RuntimeError("Preparing Python requires a build interpreter with tarfile.data_filter (Python 3.12 recommended)")
    with tarfile.open(archive, "r:gz") as source:
        for member in source.getmembers():
            parts = PurePosixPath(member.name).parts
            if not parts or parts[0] != "python" or ".." in parts:
                raise RuntimeError(f"Runtime archive path is outside python/: {member.name}")
            if member.issym() or member.islnk():
                link = PurePosixPath(member.linkname)
                base = PurePosixPath(member.name).parent if member.issym() else PurePosixPath()
                normalized = os.path.normpath(str(base / link)).replace("\\", "/")
                if link.is_absolute() or not normalized.startswith("python/"):
                    raise RuntimeError(f"Runtime archive link escapes python/: {member.name}")
        source.extractall(destination, filter="data")


def file_inventory(prefix, roots=None):
    entries = []
    paths = prefix.rglob("*") if roots is None else (
        path for root in roots for path in (prefix / root).rglob("*"))
    for path in sorted(paths):
        relative = path.relative_to(prefix)
        if relative == METADATA:
            continue
        if path.is_symlink():
            if not path.resolve().is_relative_to(prefix.resolve()):
                raise RuntimeError(f"Python runtime symlink leaves the install prefix: {relative}")
            entries.append({"path": relative.as_posix(), "symlink": os.readlink(path)})
        elif path.is_file():
            entry = {"path": relative.as_posix(), "sha256": sha256(path), "size": path.stat().st_size}
            if os.name != "nt":
                entry["executable"] = path.stat().st_mode & 0o111
            entries.append(entry)
    return entries


def verify_inventory(prefix, entries, roots=None):
    actual = file_inventory(prefix, roots)
    if {entry["path"] for entry in actual} != {entry["path"] for entry in entries}:
        raise RuntimeError("Python runtime file inventory differs from its receipt")
    if actual != entries:
        raise RuntimeError("Python runtime file integrity differs from its receipt")


def verify_wheels(wheelhouse, requirements, interpreter=None):
    # This bundled profile deliberately has unconditional exact pins. Keep
    # hashes associated with their package, and require the complete closure
    # rather than treating one valid wheel as a complete offline environment.
    canonical = lambda name: re.sub(r"[-_.]+", "-", name).lower()
    locked = {}
    logical = re.sub(rb"\\\s*\r?\n", b" ", requirements)
    for line in logical.splitlines():
        if not line.strip() or line.lstrip().startswith(b"#"):
            continue
        match = re.fullmatch(rb"([A-Za-z0-9_.-]+)==([^\s;]+)\s+(.*)", line.strip())
        if not match or b";" in line:
            raise RuntimeError("Offline Python profile requires unconditional exact package pins")
        name, version, options = match.groups()
        hashes = set(re.findall(rb"--hash=sha256:([0-9a-f]{64})", options))
        if not hashes:
            raise RuntimeError("Offline Python profile requires package hashes")
        locked[(canonical(name.decode("ascii")), version.decode("ascii"))] = hashes
    wheels = list(wheelhouse.glob("*.whl"))
    if not locked or not wheels:
        raise RuntimeError("Offline Python wheelhouse is empty or lacks a hash-locked profile")
    found = set()
    for wheel in wheels:
        parts = wheel.name.split("-")
        package = (canonical(parts[0]), parts[1]) if len(parts) >= 5 else None
        if (wheel.is_symlink() or package not in locked or package in found
                or sha256(wheel).encode("ascii") not in locked[package]):
            raise RuntimeError(f"Offline wheel does not match a profile hash: {wheel.name}")
        found.add(package)
    if found != set(locked):
        raise RuntimeError("Offline Python wheelhouse is incomplete for the locked profile")
    if interpreter is not None:
        # The universal hash lock includes wheels for several ABIs and OSes.
        # Ask this exact bundled interpreter whether each filename can run
        # here, using packaging already vendored by its pinned pip.
        checked_run([interpreter, "-I", "-B", "-c", """
import json, sys
from pip._vendor.packaging.tags import sys_tags
from pip._vendor.packaging.utils import parse_wheel_filename
compatible = set(sys_tags())
for name in json.loads(sys.argv[1]):
    if not compatible.intersection(parse_wheel_filename(name)[3]):
        raise RuntimeError('Offline wheel is not compatible with the bundled Python: ' + name)
""", json.dumps([wheel.name for wheel in wheels])])


def verify_stage(prefix):
    prefix = prefix.resolve()
    metadata = json.loads((prefix / METADATA).read_text(encoding="utf-8"))
    lock = load_lock()
    target = native_target()
    if (metadata.get("schema") != "sicnu.python-runtime/1"
            or metadata.get("python_version") != lock["python_version"]
            or metadata.get("target") != target
            or metadata.get("source", {}).get("sha256") != lock["distributions"][target]["sha256"]):
        raise RuntimeError("Python runtime receipt does not match the pinned native distribution")
    requirements = REQUIREMENTS.read_bytes()
    if metadata.get("requirements_sha256") != hashlib.sha256(requirements).hexdigest():
        raise RuntimeError("Python runtime wheelhouse was prepared for a different dependency lock")
    if (prefix / WHEELHOUSE / "requirements.lock").read_bytes() != requirements:
        raise RuntimeError("Bundled Python dependency lock differs from the source profile")
    if not executable(prefix, target).is_file():
        raise RuntimeError("Bundled Python executable is missing")
    if os.name != "nt" and not os.access(executable(prefix, target), os.X_OK):
        raise RuntimeError("Bundled Python interpreter has no executable permission")
    verify_inventory(prefix, metadata["files"], (PYTHON_ROOT, WHEELHOUSE))
    verify_wheels(prefix / WHEELHOUSE, requirements, executable(prefix, target))
    return metadata


def prepare(destination, cache, offline=False):
    lock = load_lock()
    target = native_target()
    artifact = lock["distributions"][target]
    if destination.exists():
        verify_stage(destination)
        print(f"Verified existing Python {lock['python_version']} stage: {destination}")
        return
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / unquote(Path(urlparse(artifact["url"]).path).name)
    if archive.exists():
        require_digest(archive, artifact["sha256"])
    else:
        if offline:
            raise RuntimeError(f"Offline preparation requires cached archive: {archive}")
        print(f"Downloading pinned Python {lock['python_version']} ({target})", flush=True)
        with tempfile.NamedTemporaryFile(dir=cache, suffix=".download", delete=False) as output:
            download = Path(output.name)
        try:
            with urlopen(artifact["url"], timeout=120) as response, download.open("wb") as output:
                shutil.copyfileobj(response, output)
            require_digest(download, artifact["sha256"])
            download.replace(archive)
        finally:
            download.unlink(missing_ok=True)
    requirements = REQUIREMENTS.read_bytes()
    requirements_sha = hashlib.sha256(requirements).hexdigest()
    wheel_cache = cache / "wheels" / target / requirements_sha
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".python-runtime-", dir=destination.parent) as temporary:
        prefix = Path(temporary)
        unpacked = prefix / "unpacked"
        extract_runtime(archive, unpacked)
        python_root = prefix / PYTHON_ROOT
        python_root.parent.mkdir(parents=True, exist_ok=True)
        (unpacked / "python").rename(python_root)
        unpacked.rmdir()
        python = executable(prefix, target)
        version = checked_run([python, "-I", "-B", "-c", "import platform,ssl,ctypes,venv,ensurepip; print(platform.python_version())"])
        if version != lock["python_version"]:
            raise RuntimeError(f"Downloaded interpreter version is {version}; expected {lock['python_version']}")
        if not wheel_cache.exists():
            if offline:
                raise RuntimeError(f"Offline preparation requires a cached wheelhouse: {wheel_cache}")
            wheel_cache.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix=".wheels-", dir=wheel_cache.parent) as wheel_temporary:
                print("Downloading hash-locked geospatial binary wheels", flush=True)
                checked_run([python, "-I", "-B", "-m", "pip", "--isolated", "download",
                             "--disable-pip-version-check", "--require-hashes", "--only-binary=:all:",
                             "--dest", wheel_temporary, "-r", REQUIREMENTS])
                verify_wheels(Path(wheel_temporary), requirements, python)
                shutil.copytree(wheel_temporary, wheel_cache)
        verify_wheels(wheel_cache, requirements, python)
        wheelhouse = prefix / WHEELHOUSE
        shutil.copytree(wheel_cache, wheelhouse)
        (wheelhouse / "requirements.lock").write_bytes(requirements)
        metadata = {
            "schema": "sicnu.python-runtime/1", "python_version": version, "target": target,
            "source": {"project": lock["project"], "release": lock["release"], **artifact},
            "requirements_sha256": requirements_sha,
            "interpreter": python.relative_to(prefix).as_posix(), "wheelhouse": WHEELHOUSE.as_posix(),
            "files": file_inventory(prefix, (PYTHON_ROOT, WHEELHOUSE)),
        }
        (prefix / METADATA).write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        verify_stage(prefix)
        # Rename only this script-owned temporary tree into a NEW destination.
        prefix.rename(destination)
    print(f"Prepared Python {version} and offline geospatial wheels: {destination}")


def smoke(prefix):
    metadata = verify_stage(prefix)
    python = executable(prefix, metadata["target"])
    bootstrap = json.loads(checked_run([python, "-I", "-B", "-c",
        "import json,platform,sys,ssl,ctypes,venv,ensurepip; print(json.dumps({'version':platform.python_version(),'base':sys.base_prefix}))"]))
    if bootstrap["version"] != metadata["python_version"] or not Path(bootstrap["base"]).resolve().is_relative_to(prefix.resolve()):
        raise RuntimeError("Bundled Python was not relocated with this install prefix")
    with tempfile.TemporaryDirectory(prefix="sicnu-python-package-smoke-") as temporary:
        environment = Path(temporary) / "environment"
        checked_run([python, "-I", "-B", "-m", "venv", environment])
        venv_python = environment / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
        checked_run([venv_python, "-I", "-B", "-m", "pip", "--isolated", "install",
                     "--disable-pip-version-check", "--no-index", "--find-links", prefix / WHEELHOUSE,
                     "--require-hashes", "--only-binary=:all:", "-r", prefix / WHEELHOUSE / "requirements.lock"])
        code = r'''import json, pathlib, platform, sys
import numpy as np
import rasterio
from rasterio.transform import from_origin
before = np.array([[.1,.2,.3,np.nan],[.4,.5,.6,.7],[np.nan,.1,.2,.3]], dtype='float32')
after = np.array([[.1,.55,.65,.4],[.05,.5,.25,.72],[.5,np.nan,.21,.31]], dtype='float32')
valid = np.isfinite(before) & np.isfinite(after)
changed = valid & (np.abs(after-before) > .2)
mask = np.full(before.shape, 255, dtype='uint8')
mask[valid] = changed[valid]
expected = np.array([[0,1,1,255],[1,0,1,0],[255,255,0,0]], dtype='uint8')
assert np.array_equal(mask, expected)
path = pathlib.Path(sys.argv[1]) / 'ndvi-change.tif'
grid = from_origin(300000,4000000,10,10)
with rasterio.open(path,'w',driver='GTiff',width=4,height=3,count=1,dtype='uint8',crs='EPSG:32648',transform=grid,nodata=255) as output:
    output.write(mask,1)
with rasterio.open(path) as output:
    assert np.array_equal(output.read(1),expected)
    assert output.crs.to_epsg() == 32648 and output.transform == grid
assert int(valid.sum()) == 9 and int(changed.sum()) == 4
print(json.dumps({'python':platform.python_version(),'numpy':np.__version__,'rasterio':rasterio.__version__,'valid_pixels':9,'changed_pixels':4}))
'''
        print(checked_run([venv_python, "-I", "-B", "-c", code, temporary]))
    print("Relocated bundled Python + offline GeoTIFF/NDVI smoke passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--destination", type=Path, help="new staging install-prefix directory")
    action.add_argument("--verify-stage", type=Path, help="verify complete staged or installed files")
    action.add_argument("--smoke", type=Path, help="verify and run the installed runtime with offline dependencies")
    parser.add_argument("--cache-dir", type=Path, default=ROOT / "build-logs/python-vendor-cache")
    parser.add_argument("--offline", action="store_true", help="refuse downloads; use verified cached artifacts only")
    args = parser.parse_args()
    try:
        if args.destination:
            prepare(args.destination.resolve(), args.cache_dir.resolve(), args.offline)
        elif args.verify_stage:
            verify_stage(args.verify_stage.resolve())
            print("Vendored Python stage integrity passed")
        else:
            smoke(args.smoke.resolve())
    except (RuntimeError, OSError, ValueError, tarfile.TarError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Python runtime preparation failed: {error}\n")


if __name__ == "__main__":
    main()
