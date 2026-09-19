#!/bin/sh
# build_offline_bundle.sh — assemble the offline classroom bundle (goal D7,
# extended by deployment-packaging-11/F19).
#
# usage:
#   scripts/build_offline_bundle.sh --build-dir <dir> [--out <dir>] [--version <v>]
#                                   [--max-mb <n>] [--schema {1,2}] [--skip-samples]
#                                   [--verify <bundle>]
#
# Assembles packaging/OFFLINE_BUNDLE.md's layout from a built tree, writes
# manifest.json (per-file sha256; schema /2 by default with components,
# build_options and compat provenance; --schema 1 for the legacy shape), and
# verifies it through the canonical verifier (scripts/verify_bundle_manifest.py
# — the single verify authority on POSIX/tests; the Windows twin mirrors it
# in PS). Fully local — never touches the network. Windows twin:
# scripts/build_offline_bundle.cmd.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

build_dir=""
out_dir=""
version=""
max_mb=250
schema=2
skip_samples=0
verify_path=""

usage() { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
die() { echo "build_offline_bundle: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) [ $# -ge 2 ] || usage; build_dir=$2; shift 2 ;;
    --out)       [ $# -ge 2 ] || usage; out_dir=$2; shift 2 ;;
    --version)   [ $# -ge 2 ] || usage; version=$2; shift 2 ;;
    --max-mb)    [ $# -ge 2 ] || usage; max_mb=$2; shift 2 ;;
    --schema)    [ $# -ge 2 ] || usage; schema=$2; shift 2 ;;
    --skip-samples) skip_samples=1; shift ;;
    --verify)    [ $# -ge 2 ] || usage; verify_path=$2; shift 2 ;;
    -h|--help)   usage ;;
    *) die "unknown option: $1 (see --help)" ;;
  esac
done
[ "$schema" = "1" ] || [ "$schema" = "2" ] || die "--schema must be 1 or 2"

# ---------------------------------------------------------------- verify mode
# Delegates to the canonical verifier — the same rules run on the dev host,
# inside the shipped bundle (tools/verify_bundle_manifest.py) and in tests.
if [ -n "$verify_path" ]; then
  [ -f "$script_dir/verify_bundle_manifest.py" ] || \
    die "canonical verifier missing: $script_dir/verify_bundle_manifest.py"
  exec python3 "$script_dir/verify_bundle_manifest.py" "$verify_path"
fi

# --------------------------------------------------------------- assemble mode
[ -n "$build_dir" ] || usage
[ -d "$build_dir" ] || die "build dir not found: $build_dir (configure first)"
build_dir=$(CDPATH= cd -- "$build_dir" && pwd)

cli_bin=""
gen_bin=""
for c in "$build_dir/sicnu_geo_rs_cli" "$build_dir/bin/sicnu_geo_rs_cli" "$build_dir/src/cli/sicnu_geo_rs_cli"; do
  [ -x "$c" ] && cli_bin=$c && break
done
for c in "$build_dir/tools/sicnu_generate_samples" "$build_dir/sicnu_generate_samples" "$build_dir/bin/sicnu_generate_samples"; do
  [ -x "$c" ] && gen_bin=$c && break
done
[ -n "$cli_bin" ] || die "sicnu_geo_rs_cli not found under $build_dir — build it first"
[ -n "$gen_bin" ] || die "sicnu_generate_samples not found under $build_dir — build it first"

[ -n "$version" ] || version=$( { git -C "$repo_root" describe --tags --always 2>/dev/null || true; } )
[ -n "$version" ] || version="dev"
[ -n "$out_dir" ] || out_dir="$repo_root/dist"
bundle="$out_dir/sicnu-lab-$version"
rm -rf "$bundle"
mkdir -p "$bundle/bin" "$bundle/data" "$bundle/labs"

echo "== copying binaries =="
cp "$cli_bin" "$bundle/bin/"
cp "$gen_bin" "$bundle/bin/"

echo "== generating sample data (deterministic, offline) =="
mkdir -p "$bundle/data/samples"
if [ "$skip_samples" -eq 0 ]; then
  "$gen_bin" --out="$bundle/data/samples"
fi

echo "== copying data tree =="
for entry in labs pipelines schemas help plugins processing cartography agent tools; do
  [ -e "$repo_root/data/$entry" ] && cp -R "$repo_root/data/$entry" "$bundle/data/"
done
mkdir -p "$bundle/data/fonts"
cp "$repo_root"/resources/fonts/*.ttf "$bundle/data/fonts/"

echo "== applying D7 grading overlay (calibrated to the shipped scene) =="
# The repo rules track D4/D1's fixture; the bundle's committed scene is the
# deterministic generator output, so the bundled ndvi_basics rules are the
# calibrated copy. Without this, a correct submission would fail the CRS/stats
# assertions built for a different fixture.
cp "$repo_root/packaging/bundle/labs/grading-overlay/"*.rules.json "$bundle/data/labs/grading/"

echo "== copying lab 1 =="
mkdir -p "$bundle/labs/lab1"
cp "$repo_root/packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json" "$bundle/labs/lab1/"
cp "$repo_root/packaging/bundle/labs/lab1/INSTRUCTIONS-zh.md" "$bundle/labs/lab1/"

echo "== copying one-click scripts and docs =="
for f in RUN.cmd GENERATE_SAMPLES.cmd GRADE_ALL.cmd VERIFY.cmd VERIFY.ps1 VERIFY.sh README-zh.md; do
  [ -f "$repo_root/packaging/bundle/$f" ] || die "bundle template missing: $f"
  cp "$repo_root/packaging/bundle/$f" "$bundle/"
done
mkdir -p "$bundle/tools"
cp "$script_dir/verify_bundle_manifest.py" "$bundle/tools/verify_bundle_manifest.py"

echo "== runtime data (PROJ/GDAL grids & databases; grading needs proj.db) =="
mkdir -p "$bundle/data/runtime"
proj_share=""
gdal_data="$( { gdal-config --datadir 2>/dev/null || true; } )"
for c in /usr/share/proj "${gdal_data%/gdal}/proj" /usr/share/QGIS/share/proj; do
  [ -f "$c/proj.db" ] && proj_share=$c && break
done
if [ -n "$proj_share" ]; then
  # -L dereferences symlinks: the shipped tree must be self-contained so the
  # verifier's escaping-symlink rule can never trip on host-absolute links.
  cp -RL "$proj_share" "$bundle/data/runtime/proj"
else
  die "proj.db not found (looked in /usr/share/proj et al.) — install proj-data; without it offline grading cannot identify EPSG authorities"
fi
if [ -n "$gdal_data" ] && [ -d "$gdal_data" ]; then
  cp -RL "$gdal_data" "$bundle/data/runtime/gdal"
else
  echo "note: gdal data dir not found via gdal-config — data/runtime/gdal skipped"
fi

echo "== dependency inventory (shipped vs host closure) =="
# Written BEFORE the manifest so it is hashed like any other payload file.
# Best-effort: an inventory failure warns and continues (the manifest does
# not depend on it), never blocks the classroom bundle.
if python3 "$script_dir/report_bundle_dependencies.py" --bundle "$bundle"; then
  :
else
  echo "note: dependency report failed (dependencies.json absent from this bundle)" >&2
fi

echo "== probing build/host components for the manifest =="
comp_args=""
probe_component() { # probe_component <name> <command...>  (first line only,
  name=$1; shift                #  best-effort: unresolved components are omitted)
  v=$( { timeout 60 "$@" 2>/dev/null || true; } | head -1 )
  [ -n "$v" ] && comp_args="$comp_args $name=$v" || true
}
probe_component gdal gdal-config --version
probe_component proj pkg-config --modversion proj
probe_component geos pkg-config --modversion geos
qt_version=$({ qmake6 -query QT_VERSION 2>/dev/null || qmake -query QT_VERSION 2>/dev/null || true; })
[ -n "$qt_version" ] && comp_args="$comp_args qt=$qt_version"
py_version=$(python3 -V 2>/dev/null | cut -d' ' -f2)
[ -n "$py_version" ] && comp_args="$comp_args python=$py_version"
qgis_version=$( { timeout 60 qgis --version 2>/dev/null || true; } | head -1 | cut -d' ' -f2 | sed 's/-.*//')
[ -n "$qgis_version" ] && comp_args="$comp_args qgis=$qgis_version"
# The shipped-library closure (shipped vs host-required) is reported by
# scripts/report_bundle_dependencies.py into dependencies.json — a bundle
# file like any other, hashed by this manifest.

echo "== writing manifest =="
BUNDLE_BUILD_DIR="$build_dir" python3 - "$bundle" "$version" "$max_mb" "$schema" $comp_args <<'PY'
import hashlib, json, os, sys, datetime
bundle, version, ceiling = sys.argv[1], sys.argv[2], int(sys.argv[3])
schema = sys.argv[4]
components = {}
for arg in sys.argv[5:]:
    name, _, ver = arg.partition("=")
    if name and ver:
        components[name] = {"version": ver, "source": "host"}
files = []
for base, _dirs, names in os.walk(bundle):
    for n in names:
        p = os.path.join(base, n)
        rel = os.path.relpath(p, bundle).replace(os.sep, "/")
        if rel == "manifest.json":
            continue
        files.append({"path": rel, "bytes": os.path.getsize(p),
                      "sha256": hashlib.sha256(open(p, "rb").read()).hexdigest()})
files.sort(key=lambda f: f["path"])
required = ["bin/", "data/samples/", "data/labs/grading/", "data/fonts/",
            "data/runtime/proj/", "labs/lab1/", "RUN.cmd",
            "GENERATE_SAMPLES.cmd", "GRADE_ALL.cmd", "VERIFY.cmd",
            "VERIFY.ps1", "README-zh.md", "manifest.json"]
manifest = {"schema": "sicnu.offline_bundle/1", "bundle_version": version,
            "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "size_ceiling_mb": ceiling,
            "required": required,
            "files": files}
if schema == "2":
    # /2 adds the in-bundle Linux verifier to required and records declared
    # provenance: resolved component versions and the build tree's configure
    # options (allowlisted keys only, from CMakeCache.txt when present).
    required = required + ["VERIFY.sh", "tools/verify_bundle_manifest.py"]
    build_options = {}
    cache = os.path.join(os.environ.get("BUNDLE_BUILD_DIR", ""), "CMakeCache.txt")
    allow = ("CMAKE_BUILD_TYPE", "CMAKE_GENERATOR", "ENABLE_TESTS")
    if os.path.isfile(cache):
        with open(cache, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if ":" not in line or "=" not in line:
                    continue
                key_part, _, value = line.partition("=")
                key = key_part.partition(":")[0]
                if key in allow or key.startswith("SICNU_"):
                    build_options[key] = value.strip()
    manifest = {
        "schema": "sicnu.offline_bundle/2",
        "bundle_version": version,
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "size_ceiling_mb": ceiling,
        "required": required,
        "files": files,
        "components": components,
        "build_options": build_options,
        "compat": {"min_reader_schema": 1, "bundle_kind": "lab-cli"},
    }
with open(os.path.join(bundle, "manifest.json"), "w", encoding="utf-8") as fh:
    json.dump(manifest, fh, ensure_ascii=False, indent=2)
    fh.write("\n")
print(f"manifest: {len(files)} files (schema {manifest['schema']})")
PY

"$script_dir/build_offline_bundle.sh" --verify "$bundle"
