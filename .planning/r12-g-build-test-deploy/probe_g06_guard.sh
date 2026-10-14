#!/usr/bin/env bash
# probe_g06_guard.sh — G06 RED/GREEN oracle: does the documented build entry
# point honour the cross-process build lease?
#
# Oracle (R12-G WP-G06): "模拟 3 并发 Agent 下只有一个重型构建占机器".
#
# Method (fully hermetic — no toolchain, no network):
#   1. Create a fake build dir plus a stub `cmake` on PATH that prints
#      PAYLOAD-RAN and exits 0.
#   2. Hold the resource_guard lease on that build dir in the background
#      (payload: sleep).
#   3. Ask the build entry point to build the SAME dir, with a --wait 0
#      (fail fast) so the probe never blocks.
#   4. RED   = the stub ran (the entry point ignored the lease)
#      GREEN = the entry point was refused (exit 75) and the stub never ran
set -u

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
guard="$repo_root/scripts/dev/resource_guard.py"
entry=${ENTRY:-"$repo_root/scripts/build.sh"}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/build" "$work/bin"

# --- stub cmake: proves whether the payload was reached -------------------
cat > "$work/bin/cmake" <<'STUB'
#!/usr/bin/env bash
echo "PAYLOAD-RAN $*"
exit 0
STUB
chmod +x "$work/bin/cmake"

py=$(command -v python3 || command -v python || true)
if [ -z "$py" ]; then echo "PROBE: NOT_RUN (no python)"; exit 77; fi

# --- 1. hold the lease ----------------------------------------------------
"$py" "$guard" --lock-dir "$work/build" --wait 0 -- python -c "import time;time.sleep(8)" \
  > "$work/holder.log" 2>&1 &
holder=$!
# wait until the lock file is actually visible
for _ in $(seq 1 60); do
  [ -f "$work/build.agent-build.lock" ] && break
  sleep 0.1
done
if [ ! -f "$work/build.agent-build.lock" ]; then
  echo "PROBE: SETUP-FAILED (lease never taken)"; kill "$holder" 2>/dev/null; exit 77
fi

# --- 2. ask the entry point to build the same dir -------------------------
out=$(PATH="$work/bin:$PATH" SICNU_BUILD_GUARD_WAIT=0 \
      bash "$entry" build --build-dir "$work/build" 2>&1)
rc=$?

wait "$holder" 2>/dev/null || true

echo "--- entry point: $entry"
echo "--- exit code: $rc"
echo "--- output:"; echo "$out"

if echo "$out" | grep -q "PAYLOAD-RAN"; then
  echo "PROBE: RED — the entry point built while another process held the lease"
  echo "       (no cross-process serialisation; -j2 cap alone does not bound"
  echo "        the machine when several agents build at once)"
  exit 1
fi
if [ "$rc" = 75 ]; then
  echo "PROBE: GREEN — refused with EX_TEMPFAIL(75); payload never ran"
  exit 0
fi
echo "PROBE: INCONCLUSIVE — payload did not run but exit code was $rc (expected 75)"
exit 2
