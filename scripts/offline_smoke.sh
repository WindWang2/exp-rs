#!/bin/sh
# offline_smoke.sh — D7 offline acceptance smoke (POSIX lane).
#
# Exercises the classroom story exactly as a machine without a source tree
# would see it, with the offline gate engaged and proxy variables unset so any
# accidental network attempt fails loudly:
#
#   1. build the offline bundle (deterministic samples generated in-bundle)
#   2. verify the bundle manifest (integrity, required prefixes, ceiling)
#   3. run lab 1 from inside the bundle: NDVI pipeline, offline
#   4. grade the artifact against the bundle's calibrated ndvi_basics rules
#      (single-submission path + peak-RSS baseline)
#   5. batch-grade 60 synthetic submissions + 1 corrupt file:
#      - CSV has 61 data rows, CRLF, UTF-8 BOM
#      - the corrupt submission is isolated as an "error" row
#      - peak RSS(batch) <= peak RSS(single) * 1.25 (streaming bound)
#
# usage: scripts/offline_smoke.sh [--build-dir build-dev] [--out <dir>]
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

build_dir="$repo_root/build-dev"
out_root="$repo_root/dist/smoke"
while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) build_dir=$2; shift 2 ;;
    --out) out_root=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

CLI_NAME=sicnu_geo_rs_cli
GEN_NAME=sicnu_generate_samples
[ -x "$build_dir/$CLI_NAME" ] || { echo "smoke: CLI not built: $build_dir/$CLI_NAME" >&2; exit 1; }
[ -x "$build_dir/tools/$GEN_NAME" ] || { echo "smoke: generator not built: $build_dir/tools/$GEN_NAME" >&2; exit 1; }

fail() { echo "SMOKE FAIL: $*" >&2; exit 1; }

# Peak-RSS measurement without GNU time: python's getrusage(RUSAGE_CHILDREN)
# reports the child's ru_maxrss (KiB on Linux) and propagates the exit code.
measure_rss() { # measure_rss <stdout-file> <stderr-file> <rss-file> <cmd...>
  python3 - "$@" <<'PY'
import resource, subprocess, sys
out, err, rssf, cmd = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
with open(out, 'wb') as fo, open(err, 'wb') as fe:
    rc = subprocess.call(cmd, stdout=fo, stderr=fe)
with open(rssf, 'w') as fr:
    fr.write(str(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss))
sys.exit(rc)
PY
}


echo "== offline smoke (zero-network discipline) =="
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY all_proxy
export QT_QPA_PLATFORM=offscreen
# The gate engages via the --offline flag on every call below; SICNU_OFFLINE
# mirrors it the way the bundle scripts set it.
export SICNU_OFFLINE=1

cd "$repo_root"
mkdir -p "$out_root"

echo "== [1/5] bundle build (samples generated in-bundle) =="
"$script_dir/build_offline_bundle.sh" --build-dir "$build_dir" --out "$out_root" \
  || fail "bundle build failed"
bundle_dir=$(ls -d "$out_root"/sicnu-lab-* | tail -1)

echo "== [2/5] manifest verify =="
"$script_dir/build_offline_bundle.sh" --verify "$bundle_dir" || fail "manifest verify failed"

echo "== [3/5] lab 1 from inside the bundle (offline pipeline + grade) =="
export SICNU_LAB_RULES_DIR="$bundle_dir/data/labs/grading"
export PROJ_DATA="$bundle_dir/data/runtime/proj"
export GDAL_DATA="$bundle_dir/data/runtime/gdal"
PATH="$bundle_dir/bin:$PATH"
export PATH
cd "$bundle_dir"
mkdir -p output

"$CLI_NAME" --offline --pipeline "labs/lab1/lab1_ndvi.pipeline.json" \
  > "$out_root/pipeline.log" 2>&1 || { tail -5 "$out_root/pipeline.log"; fail "pipeline failed"; }
[ -f output/lab1_ndvi.tif ] || fail "pipeline produced no artifact"

measure_rss "$out_root/grade.log" "$out_root/grade.time" "$out_root/grade.rss" \
  "$CLI_NAME" --offline lab --lab ndvi_basics --grade output/lab1_ndvi.tif \
  --out output/lab1_report.json \
  || { tail -8 "$out_root/grade.log"; fail "single grade did not pass"; }
RSS_SINGLE_KB=$(cat "$out_root/grade.rss")
[ -n "$RSS_SINGLE_KB" ] || fail "could not read single-grade RSS"
echo "   ok (peak RSS ${RSS_SINGLE_KB} KiB)"

echo "== [4/5] batch: 60 synthetic submissions + 1 corrupt =="
subs="$out_root/submissions"
rm -rf "$subs"; mkdir -p "$subs"
i=1
while [ $i -le 60 ]; do
  id=$(printf 's%03d' $i)
  cp output/lab1_ndvi.tif "$subs/$id.tif"
  i=$((i + 1))
done
printf 'this is not a raster' > "$subs/s61_corrupt.tif"

measure_rss "$out_root/batch.log" "$out_root/batch.time" "$out_root/batch.rss" \
  "$CLI_NAME" --offline lab --lab ndvi_basics --batch "$subs" --csv "$subs/grades.csv" \
  || { tail -5 "$out_root/batch.log"; fail "batch run exited non-zero (isolation row present?)"; }
RSS_BATCH_KB=$(cat "$out_root/batch.rss")

python3 - "$subs/grades.csv" "$RSS_SINGLE_KB" "$RSS_BATCH_KB" <<'PY'
import sys
csv_path, rss_single, rss_batch = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
raw = open(csv_path, 'rb').read()
assert raw.startswith(b'\xef\xbb\xbf'), 'CSV missing UTF-8 BOM'
assert b'\r\n' in raw, 'CSV rows are not CRLF'
text = raw.decode('utf-8-sig')
lines = [l for l in text.split('\r\n') if l]
assert lines[0] == 'student_id,lab_id,score,verdict,top_deduction,artifact_path', lines[0]
rows = lines[1:]
assert len(rows) == 61, f'expected 61 data rows (60 graded + 1 corrupt), got {len(rows)}'
s001 = [r for r in rows if r.startswith('s001,')]
assert s001 and ',pass,' in s001[0], f's001 not a pass row: {s001}'
corrupt = [r for r in rows if r.startswith('s61_corrupt,')]
# Real grader semantics: a non-raster comes back "unverifiable" (typed, no
# abort); the ",error," isolation row covers hard exceptions from the engine.
assert corrupt and (',unverifiable,' in corrupt[0] or ',error,' in corrupt[0]), \
    'corrupt submission neither unverifiable nor isolated'
assert len(corrupt) == 1 and ',pass,' not in corrupt[0] and ',fail,' not in corrupt[0]
graded = sum(1 for r in rows if ',pass,' in r or ',fail,' in r)
assert graded == 60, f'expected 60 graded rows, got {graded}'
ratio = rss_batch / max(rss_single, 1)
assert ratio <= 1.25, f'streaming bound broken: batch RSS {rss_batch} vs single {rss_single}'
print(f'   ok: 61 rows (60 graded, 1 isolated), '
      f'RSS batch {rss_batch} KiB vs single {rss_single} KiB (x{ratio:.2f})')
PY
echo "   ok"

echo
echo "SMOKE PASS (offline): bundle verified; lab 1 ran and graded from the bundle;"
echo "61-row batch isolated the corrupt submission; memory bounded by the"
echo "single-raster baseline."
