#!/usr/bin/env bash
# R12-G R1 P1 复现器：R1-01（REQUIRE_GUARD 被进程替换吞掉）与 R1-02（--jobs 绕过 -j 上限）
# 全部使用 stub cmake：只要 payload 跑了就会打印 PAYLOAD-RAN。
set -u
REPO="/c/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test"
STUB="${TEMP:-/tmp}/r12g-r1fix/stub"
BD="${TEMP:-/tmp}/r12g-r1fix/bd"
LOG="${TEMP:-/tmp}/r12g-r1fix/logs"
mkdir -p "$STUB" "$BD" "$LOG"
printf '#!/usr/bin/env bash\necho "PAYLOAD-RAN: $*"\nexit 0\n' > "$STUB/cmake"
chmod +x "$STUB/cmake"

echo "=== R1-02a: --jobs 8（未抬 cap）应被拒绝 ==="
SICNU_BUILD_LOG_DIR="$LOG" "$REPO/scripts/build.sh" build --build-dir "$BD" --jobs 8 2>&1 | tail -3
echo "exit=${PIPESTATUS[0]}"

echo
echo "=== R1-02b: --jobs 2（合法）应放行 ==="
SICNU_BUILD_LOG_DIR="$LOG" "$REPO/scripts/build.sh" build --build-dir "$BD" --jobs 2 2>&1 | tail -2
echo "exit=${PIPESTATUS[0]}"

echo
echo "=== R1-02c: SICNU_BUILD_JOBS_CAP=99 --jobs 8（刻意抬 cap）应放行 ==="
SICNU_BUILD_JOBS_CAP=99 SICNU_BUILD_LOG_DIR="$LOG" \
  "$REPO/scripts/build.sh" build --build-dir "$BD" --jobs 8 2>&1 | tail -2
echo "exit=${PIPESTATUS[0]}"

echo
echo "=== R1-01: 无 python + REQUIRE_GUARD=1 必须拒绝（不能 exit 0）==="
# 最小 PATH：只有 bash 核心工具 + stub cmake，没有 python
env -i PATH="/usr/bin:/bin:$STUB" HOME="${TEMP:-/tmp}" TMPDIR="${TEMP:-/tmp}" \
    SICNU_BUILD_REQUIRE_GUARD=1 SICNU_BUILD_LOG_DIR="$LOG" SICNU_BUILD_GUARD_WAIT=0 \
    bash "$REPO/scripts/build.sh" build --build-dir "$BD" 2>&1 | tail -4
echo "exit=${PIPESTATUS[0]}"

echo
echo "=== R1-06: REQUIRE_GUARD 与 NO_GUARD 同时设定应为显式冲突 ==="
SICNU_BUILD_REQUIRE_GUARD=1 SICNU_BUILD_NO_GUARD=1 SICNU_BUILD_LOG_DIR="$LOG" \
  "$REPO/scripts/build.sh" build --build-dir "$BD" 2>&1 | tail -3
echo "exit=${PIPESTATUS[0]}"

echo
echo "=== 守卫自身 --max-parallel 护栏 ==="
python "$REPO/scripts/dev/resource_guard.py" --lock-dir "$BD" --parallel 8 -- echo RAN 2>&1 | tail -2
echo "default_ceiling_exit=${PIPESTATUS[0]}"
python "$REPO/scripts/dev/resource_guard.py" --lock-dir "$BD" --parallel 8 --max-parallel 99 -- echo RAN 2>&1 | tail -2
echo "raised_ceiling_exit=${PIPESTATUS[0]}"
