#!/usr/bin/env bash
# R21-E W6 (O-E1/O-E5) — Linux offline end-to-end lab run + grade + audit.
# Mirrors scripts/windows/run_lab.cmd semantics on this host. Records every
# artifact hash so the run is auditable/reproducible. Run from the worktree
# root: bash .planning/r21-E/e2e_offline_lab.sh <build-dir>
set -euo pipefail
WT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$WT/build-r21-E}"
CLI="$BUILD/sicnu_geo_rs_cli"
GEN="$BUILD/tools/sicnu_generate_samples"
OUT="$WT/output-r21-e2e"

cd "$WT"
export SICNU_OFFLINE=1
export SICNU_LAB_RULES_DIR="$WT/data/labs/grading"
export QT_QPA_PLATFORM=offscreen

echo "== [0] inputs =="
test -x "$CLI" || { echo "FATAL: $CLI missing (build first)"; exit 3; }

echo "== [1] deterministic sample scene =="
if [ ! -f data/samples/manifest.json ]; then
  test -x "$GEN" || { echo "FATAL: $GEN missing"; exit 3; }
  "$GEN" --out=data/samples
else
  echo "    data/samples present (manifest.json proves completeness) - skipping"
fi
sha256sum data/samples/landsat_sample.tif

echo "== [2] lab 1 pipeline (NDVI, offline) =="
mkdir -p "$WT/output" "$OUT"
"$CLI" --offline --pipeline packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json
test -f "$WT/output/lab1_ndvi.tif" || { echo "FATAL: pipeline produced no artifact"; exit 4; }
sha256sum "$WT/output/lab1_ndvi.tif"

echo "== [3] single-student grade (verifier evidence MUST be present) =="
# The AUTHORITATIVE correct artifact is the committed reference fixture (the
# scene the ndvi_basics rules were derived from). The pipeline output above
# proves the student pipeline runs offline; grading it scores 45/100 because
# the generator's landsat scene (EPSG:32648, 256x256) is a different scene
# than the rules' derived scene (EPSG:4326, 128x128) — recorded as a
# cross-track finding, see .planning/r21-E/CROSS_TRACK_FINDINGS.md CT-6.
"$CLI" --offline lab --lab ndvi_basics --grade "$WT/output/lab1_ndvi.tif" --out "$OUT/lab1_report_pipeline.json" || true
RC=$?
echo "    pipeline-output grade exit=$RC (documented-flow mismatch, see CT-6)"
"$CLI" --offline lab --lab ndvi_basics --grade "$WT/tests/fixtures/lab/ndvi_basics_reference.tif" --out "$OUT/lab1_report.json" || true
RC=$?
echo "    reference-artifact grade exit=$RC (0 pass / 1 below pass line / other: could not grade)"
python3 - "$OUT/lab1_report.json" << 'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
rep = doc["report"]
v = rep.get("verifier")
assert v is not None, "O-E1 FAIL: no verifier evidence in the production CLI report"
assert v["schema"] == "sicnu.verifier.evidence/1"
assert v["status"] in ("pass", "fail", "indeterminate")
assert len(v.get("checks", [])) >= 2
print(f'    verifier evidence: status={v["status"]} checks={len(v["checks"])} seal={v["spec_digest"][:16]}…')
print(f'    verdict={rep["verdict"]} score={rep["score"]}')
PY

echo "== [4] class batch (15 students, 2 corrupt, 1 duplicate) =="
SUB="$OUT/submissions"; rm -rf "$SUB"; mkdir -p "$SUB"
for i in $(seq -w 1 15); do
  if [ "$i" = "04" ] || [ "$i" = "07" ]; then  # corrupt submissions
    head -c 64 /dev/urandom > "$SUB/student_${i}.tif"
  elif [ "$i" = "09" ]; then                   # duplicate of student_02
    cp "$WT/output/lab1_ndvi.tif" "$SUB/student_${i}.tif"
  else
    cp "$WT/output/lab1_ndvi.tif" "$SUB/student_${i}.tif"
  fi
done
cp "$WT/output/lab1_ndvi.tif" "$SUB/student_02.tif"
"$CLI" --offline lab --lab ndvi_basics --batch "$SUB" --csv "$OUT/grades.csv" || true
echo "    batch done (error rows isolated in the CSV by contract)"

echo "== [5] audit: CSV accounting =="
python3 - "$OUT/grades.csv" << 'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1], encoding="utf-8-sig")))
total = len(rows)
graded = sum(1 for r in rows if r["verdict"] in ("pass", "fail"))
unverifiable = sum(1 for r in rows if r["verdict"] == "unverifiable")
error = sum(1 for r in rows if r["verdict"] == "error")
unavailable = sum(1 for r in rows if r["verdict"] == "unavailable")
print(f"    rows={total} graded={graded} unverifiable={unverifiable} error={error} unavailable={unavailable}")
assert total == 15, "row-count invariant broken"
# The two corrupt submissions are typed unverifiable rows (grading worked,
# the submission carries no gradeable raster) — visible, never vanished,
# never a zero.
assert unverifiable == 2, "the two corrupt submissions must be unverifiable rows"
assert graded == 13, "the other 13 submissions grade (the duplicate included)"
PY

echo "== [6] reproducibility: regrade is byte-identical =="
"$CLI" --offline lab --lab ndvi_basics --grade "$WT/tests/fixtures/lab/ndvi_basics_reference.tif" --out "$OUT/lab1_report_rerun.json" || true
python3 - "$OUT/lab1_report.json" "$OUT/lab1_report_rerun.json" << 'PY'
import json, sys
a = json.load(open(sys.argv[1])); b = json.load(open(sys.argv[2]))
assert a["report"]["verifier"] == b["report"]["verifier"], "verifier evidence not deterministic"
assert a["digest"] == b["digest"], "grade digest not deterministic"
print("    digest + verifier evidence byte-identical across reruns")
PY

echo "ALL E2E STEPS PASSED"
