#!/bin/sh
# build_offline_bundle.sh — assemble the offline classroom bundle (goal D7).
#
# usage:
#   scripts/build_offline_bundle.sh --build-dir <dir> [--out <dir>] [--version <v>]
#                                   [--max-mb <n>] [--skip-samples] [--verify <bundle>]
#
# Assembles packaging/OFFLINE_BUNDLE.md's layout from a built tree, writes
# manifest.json (per-file sha256), and verifies it. Fully local — never
# touches the network. Windows twin: scripts/build_offline_bundle.cmd.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

build_dir=""
out_dir=""
version=""
max_mb=250
skip_samples=0
verify_path=""

usage() { sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
die() { echo "build_offline_bundle: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) [ $# -ge 2 ] || usage; build_dir=$2; shift 2 ;;
    --out)       [ $# -ge 2 ] || usage; out_dir=$2; shift 2 ;;
    --version)   [ $# -ge 2 ] || usage; version=$2; shift 2 ;;
    --max-mb)    [ $# -ge 2 ] || usage; max_mb=$2; shift 2 ;;
    --skip-samples) skip_samples=1; shift ;;
    --verify)    [ $# -ge 2 ] || usage; verify_path=$2; shift 2 ;;
    -h|--help)   usage ;;
    *) die "unknown option: $1 (see --help)" ;;
  esac
done

sha256_of() { sha256sum "$1" | cut -d' ' -f1; }

# ---------------------------------------------------------------- verify mode
if [ -n "$verify_path" ]; then
  [ -f "$verify_path/manifest.json" ] || die "no manifest.json under $verify_path"
  python3 - "$verify_path" <<'PY'
import hashlib, json, os, sys
root = sys.argv[1]
m = json.load(open(os.path.join(root, "manifest.json")))
assert m.get("schema") == "sicnu.offline_bundle/1", "bad manifest schema"
bad, total = [], 0
for f in m.get("files", []):
    p = os.path.join(root, f["path"])
    if not os.path.isfile(p):
        bad.append(f"missing {f['path']}"); continue
    b = os.path.getsize(p); total += b
    if b != f["bytes"]:
        bad.append(f"size mismatch {f['path']}: {b} != {f['bytes']}"); continue
    h = hashlib.sha256(open(p, "rb").read()).hexdigest()
    if h != f["sha256"]:
        bad.append(f"sha256 mismatch {f['path']}")
for req in m.get("required", []):
    if req.endswith("/"):
        if not any(f["path"].startswith(req) for f in m.get("files", [])):
            bad.append(f"required prefix empty: {req}")
    elif not os.path.exists(os.path.join(root, req)):
        bad.append(f"required missing: {req}")
ceiling = int(m.get("size_ceiling_mb", 250))
mb = total / (1024 * 1024)
print(f"BUNDLE VERIFY {'FAIL' if bad else 'PASS'} {root} "
      f"({len(m.get('files', []))} files, {mb:.1f} MB / ceiling {ceiling} MB)")
for b in bad: print("  " + b)
if mb > ceiling: print(f"  size {mb:.1f} MB exceeds ceiling {ceiling} MB")
sys.exit(1 if (bad or mb > ceiling) else 0)
PY
  exit $?
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
if [ "$skip_samples" -eq 0 ]; then
  ( cd "$bundle" && "$gen_bin" "$bundle/data/samples" )
else
  mkdir -p "$bundle/data/samples"
fi

echo "== copying data tree =="
for entry in labs pipelines schemas help plugins processing cartography agent tools; do
  [ -e "$repo_root/data/$entry" ] && cp -R "$repo_root/data/$entry" "$bundle/data/"
done
mkdir -p "$bundle/data/fonts"
cp "$repo_root"/resources/fonts/*.ttf "$bundle/data/fonts/"

echo "== copying lab 1 =="
mkdir -p "$bundle/labs/lab1"
cp "$repo_root/packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json" "$bundle/labs/lab1/"
cp "$repo_root/packaging/bundle/labs/lab1/INSTRUCTIONS-zh.md" "$bundle/labs/lab1/"

echo "== copying one-click scripts and docs =="
cp "$repo_root/packaging/bundle/RUN.cmd" "$bundle/"
cp "$repo_root/packaging/bundle/GENERATE_SAMPLES.cmd" "$bundle/"
cp "$repo_root/packaging/bundle/GRADE_ALL.cmd" "$bundle/"
cp "$repo_root/packaging/bundle/README-zh.md" "$bundle/"

echo "== writing manifest =="
python3 - "$bundle" "$version" "$max_mb" <<'PY'
import hashlib, json, os, sys, datetime
bundle, version, ceiling = sys.argv[1], sys.argv[2], int(sys.argv[3])
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
manifest = {"schema": "sicnu.offline_bundle/1", "bundle_version": version,
            "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "size_ceiling_mb": ceiling,
            "required": ["bin/", "data/samples/", "data/labs/grading/", "data/fonts/",
                         "labs/lab1/", "RUN.cmd", "GENERATE_SAMPLES.cmd", "GRADE_ALL.cmd",
                         "README-zh.md", "manifest.json"],
            "files": files}
with open(os.path.join(bundle, "manifest.json"), "w", encoding="utf-8") as fh:
    json.dump(manifest, fh, ensure_ascii=False, indent=2)
    fh.write("\n")
print(f"manifest: {len(files)} files")
PY

"$script_dir/build_offline_bundle.sh" --verify "$bundle"
