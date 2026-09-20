#!/usr/bin/env bash
# build.sh — resource-safe local build/test wrapper (POSIX lane).
#
# One entry point for the local development loop with the project's resource
# discipline baked in (ADR 0147 / D7, mirrored by scripts/windows/_env.cmd):
#
#   * parallelism: default -j1, hard cap -j2, -j3+ refused (never silently run);
#     inherited CMAKE_BUILD_PARALLEL_LEVEL / CTEST_PARALLEL_LEVEL are clamped;
#   * ctest always -j1 with QT_QPA_PLATFORM=offscreen;
#   * every configure/build/test run tees a log plus a failure SUMMARY.txt;
#   * "smoke" configures from an EMPTY directory so a stale CMakeCache can
#     never masquerade as a working configure.
#
# usage:
#   scripts/build.sh selftest                 # hermetic: no cmake/compiler needed
#   scripts/build.sh doctor [--build-dir D]   # dependency report from a tree
#   scripts/build.sh configure [--build-dir D] [--preset P] [--build-type T]
#                                 [--base-cache <CMakeCache.txt>] [-- <extra -D...>]
#   scripts/build.sh build [--build-dir D] [--jobs N] [target ...]
#   scripts/build.sh test  [--build-dir D] [-R <regex>]
#   scripts/build.sh smoke [--build-dir D] [--base-cache <CMakeCache.txt>]
#   scripts/build.sh preset-check             # CMakePresets.json hygiene assertions
#   scripts/build.sh dep-fixture-test         # dependency-doctor fixture gate
#
# Nothing in this script downloads dependencies. On machines configured through
# vcpkg manifest mode, forward the installed tree explicitly (smoke --base-cache
# does this for you from an existing configured tree).
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)

# ---------------------------------------------------------------------------
# Resource policy — single source of truth for this wrapper.
# ---------------------------------------------------------------------------
JOBS_DEFAULT=1
JOBS_CAP=${SICNU_BUILD_JOBS_CAP:-2}

log_dir() { printf '%s\n' "${SICNU_BUILD_LOG_DIR:-$repo_root/build-logs}"; }

fail() { echo "build.sh: $*" >&2; exit 1; }
note() { echo "build.sh: $*" >&2; }

# Resolve the effective parallel job count.
#   explicit SICNU_BUILD_JOBS / --jobs: 1..JOBS_CAP accepted, above refused
#     (exit 2); 0/invalid -> default.
#   inherited CMAKE_BUILD_PARALLEL_LEVEL / CTEST_PARALLEL_LEVEL above the cap
#     are clamped (a stale environment must not defeat the cap).
# Prints the job count on stdout; diagnostics go to stderr.
resolve_jobs() {
    local req=${SICNU_BUILD_JOBS:-${1:-}}
    case "$req" in
        ''|*[!0-9]*) req=$JOBS_DEFAULT ;;
        0) req=$JOBS_DEFAULT ;;
    esac
    if (( req > JOBS_CAP )); then
        echo "build.sh: refusing jobs=$req — parallel cap is $JOBS_CAP (raise only with SICNU_BUILD_JOBS_CAP)" >&2
        exit 2
    fi
    local bp cp
    bp=$(_clamp_env "${CMAKE_BUILD_PARALLEL_LEVEL:-}")
    cp=$(_clamp_env "${CTEST_PARALLEL_LEVEL:-}")
    local inherited_bp=${CMAKE_BUILD_PARALLEL_LEVEL:-}
    local inherited_cp=${CTEST_PARALLEL_LEVEL:-}
    if [ -n "$inherited_bp" ] && [ "$inherited_bp" != "$bp" ]; then
        note "clamped CMAKE_BUILD_PARALLEL_LEVEL=$inherited_bp -> $bp"
    fi
    if [ -n "$inherited_cp" ] && [ "$inherited_cp" != "$cp" ]; then
        note "clamped CTEST_PARALLEL_LEVEL=$inherited_cp -> $cp"
    fi
    CMAKE_BUILD_PARALLEL_LEVEL=$bp
    CTEST_PARALLEL_LEVEL=$cp
    export CMAKE_BUILD_PARALLEL_LEVEL CTEST_PARALLEL_LEVEL
    printf '%s\n' "$req"
}

_clamp_env() {
    local v=$1
    case "$v" in
        ''|*[!0-9]*) echo "$JOBS_DEFAULT" ;;
        *) if (( v > JOBS_CAP )); then echo "$JOBS_CAP"; else echo "$v"; fi ;;
    esac
}

# quote_probe <arg>...: prints each argument on its own line, bracketed. Used by
# selftest to prove paths with spaces/Unicode survive as single arguments.
quote_probe() {
    local a
    for a in "$@"; do printf '<%s>\n' "$a"; done
}

# build_command <build-dir> <jobs> [targets...] — echo the exact argv the build
# runner executes (kept as one function so selftest can assert its shape).
build_command() {
    local d=$1 j=$2; shift 2
    if [ $# -gt 0 ]; then
        printf 'cmake --build %s --parallel %s -- %s\n' "$d" "$j" "$*"
    else
        printf 'cmake --build %s --parallel %s\n' "$d" "$j"
    fi
}

# test_command <build-dir> [regex] — echo the exact argv the test runner executes.
test_command() {
    local d=$1 r=${2:-}
    if [ -n "$r" ]; then
        printf 'QT_QPA_PLATFORM=offscreen ctest --test-dir %s -R %s -j1 --output-on-failure\n' "$d" "$r"
    else
        printf 'QT_QPA_PLATFORM=offscreen ctest --test-dir %s -j1 --output-on-failure\n' "$d"
    fi
}

# run_logged <log-name> <cmd...>: tee output to the log dir; on failure keep a
# SUMMARY.txt with a compact error extract plus the tail.
run_logged() {
    local name=$1; shift
    local dir; dir="$(log_dir)/$name"
    mkdir -p "$dir"
    local rc=0
    "$@" 2>&1 | tee "$dir/run.log" || rc=${PIPESTATUS[0]}
    if [ "$rc" != 0 ]; then
        {
            echo "command: $*"
            echo "exit: $rc"
            echo "--- error/failed lines ---"
            grep -nE 'error:|ERROR:|FAILED:|failed|ninja: build stopped|CMake Error' "$dir/run.log" | tail -40 || true
            echo "--- last 20 lines ---"
            tail -20 "$dir/run.log"
        } > "$dir/SUMMARY.txt"
        echo "build.sh: FAILED ($name) — see $dir/SUMMARY.txt" >&2
    fi
    return "$rc"
}

# ---------------------------------------------------------------------------
# selftest — hermetic portability + resource-cap assertions (no toolchain used)
# ---------------------------------------------------------------------------
selftest_fail() { echo "selftest FAIL: $*" >&2; SELFTEST_FAILED=1; }
SELFTEST_FAILED=0
st_assert_eq() { # st_assert_eq <expected> <actual> <what>
    if [ "$1" = "$2" ]; then echo "  ok: $3"; else selftest_fail "$3: expected '$1', got '$2'"; fi
}

cmd_selftest() {
    echo "build.sh selftest: resource caps + path/space/Unicode quoting"

    # --- 1. job resolution matrix ------------------------------------------
    st_assert_eq 1 "$(env -u CMAKE_BUILD_PARALLEL_LEVEL -u CTEST_PARALLEL_LEVEL SICNU_BUILD_JOBS= "$0" --print-jobs)" "default jobs = 1"
    st_assert_eq 1 "$(SICNU_BUILD_JOBS=1 "$0" --print-jobs)" "jobs 1"
    st_assert_eq 2 "$(SICNU_BUILD_JOBS=2 "$0" --print-jobs)" "jobs 2 (cap)"
    local rc=0
    SICNU_BUILD_JOBS=3 "$0" --print-jobs >/dev/null 2>&1 || rc=$?
    st_assert_eq 2 "$rc" "jobs 3 refused with exit 2"
    rc=0
    SICNU_BUILD_JOBS=8 "$0" --print-jobs >/dev/null 2>&1 || rc=$?
    st_assert_eq 2 "$rc" "jobs 8 refused with exit 2"
    st_assert_eq 2 "$(SICNU_BUILD_JOBS=2 CMAKE_BUILD_PARALLEL_LEVEL=8 "$0" --print-jobs 2>/dev/null)" "inherited CMAKE_BUILD_PARALLEL_LEVEL=8 clamped to cap"
    st_assert_eq 1 "$(env -u CMAKE_BUILD_PARALLEL_LEVEL CTEST_PARALLEL_LEVEL=8 "$0" --print-jobs 2>/dev/null)" "inherited CTEST_PARALLEL_LEVEL clamped"

    # --- 2. space/Unicode quoting -------------------------------------------
    # A directory name with spaces, combining Unicode and CJK: every historical
    # defect class (word splitting, unquoted expansion) shows up here.
    local tmpbase=${TMPDIR:-/tmp}
    local fixture="$tmpbase/sü bü ild 路径/build portability"
    rm -rf "$fixture" 2>/dev/null || true
    if ! mkdir -p "$fixture"; then selftest_fail "cannot create fixture dir"; return 1; fi
    st_assert_eq 3 "$(quote_probe "$fixture/a b" "$fixture/路径 c" plain | wc -l | tr -d ' ')" "space/Unicode args stay one argument each"
    case "$(quote_probe "$fixture/a b" "$fixture/路径 c" plain)" in
        *"<$fixture/a b>"*"<$fixture/路径 c>"*"<plain>"*) echo "  ok: argument values preserved verbatim" ;;
        *) selftest_fail "argument values mangled by quoting" ;;
    esac
    # The wrapper must operate from a spaced/Unicode cwd.
    if ( cd "$fixture" && "$script_dir/build.sh" --print-jobs >/dev/null 2>&1 ); then
        echo "  ok: wrapper runs from spaced/Unicode cwd"
    else
        selftest_fail "wrapper failed from spaced/Unicode cwd"
    fi
    # A repo root containing spaces must resolve through the same discipline.
    st_assert_eq "$repo_root" "$(cd "$fixture" && SICNU_REPO_ROOT="$repo_root" bash -c 'cd -- "$SICNU_REPO_ROOT" && pwd')" "repo root with spaces resolves"
    rm -rf "$fixture" 2>/dev/null || true

    # --- 3. command shape ----------------------------------------------------
    local built_cmd ctest_cmd
    built_cmd=$(build_command "$repo_root/build-logs-selftest" 2 target_a target_b)
    case "$built_cmd" in
        *"--parallel 2"*) echo "  ok: build command pins --parallel 2" ;;
        *) selftest_fail "build command missing cap: $built_cmd" ;;
    esac
    case "$built_cmd" in
        *" -j3"*|*" -j4"*|*" -j8"*) selftest_fail "build command exceeds cap: $built_cmd" ;;
        *) echo "  ok: build command never exceeds cap" ;;
    esac
    ctest_cmd=$(test_command "$repo_root/build-logs-selftest" 'Family.*')
    case "$ctest_cmd" in
        *"-j1"*) echo "  ok: ctest pinned -j1" ;;
        *) selftest_fail "ctest missing -j1: $ctest_cmd" ;;
    esac
    case "$ctest_cmd" in
        *"QT_QPA_PLATFORM=offscreen"*) echo "  ok: ctest sets offscreen platform" ;;
        *) selftest_fail "ctest missing QT_QPA_PLATFORM=offscreen" ;;
    esac

    if [ "$SELFTEST_FAILED" = 0 ]; then
        echo "selftest: ALL PASS"
        return 0
    fi
    echo "selftest: FAILED" >&2
    return 1
}

# ---------------------------------------------------------------------------
# doctor — dependency report from an existing configured tree (WP1 companion)
# ---------------------------------------------------------------------------
cmd_doctor() {
    local build_dir="$repo_root/build-dev"
    while [ $# -gt 0 ]; do
        case "$1" in
            --build-dir) build_dir=$2; shift 2 ;;
            *) fail "doctor: unknown option: $1" ;;
        esac
    done
    local cache="$build_dir/CMakeCache.txt"
    [ -f "$cache" ] || fail "doctor: not configured: $cache (run: scripts/build.sh configure --build-dir $build_dir)"
    echo "=== SICNU dependency summary ($build_dir) ==="
    local k v
    for k in CMAKE_BUILD_TYPE CMAKE_GENERATOR CMAKE_TOOLCHAIN_FILE VCPKG_TARGET_TRIPLET \
             Qt6_DIR Qt6Keychain_DIR QCA_INCLUDE_DIR BISON_EXECUTABLE FLEX_EXECUTABLE \
             CMAKE_PREFIX_PATH VCPKG_INSTALLED_DIR; do
        v=$(sed -n "s/^$k:[A-Z]*=//p" "$cache" | tail -1)
        if [ -n "$v" ]; then printf '  %-26s %s\n' "$k" "$v"; fi
    done
    echo "  (library versions are printed by the configure-time summary — see docs/development/build-from-scratch.md)"
    # Actionable presence checks for the classic Windows miss spots.
    local missing=0 qt vc
    qt=$(sed -n "s/^Qt6_DIR:[A-Z]*=//p" "$cache" | tail -1)
    if [ -n "$qt" ]; then
        if [ -d "$qt" ]; then printf '  [ok] %-20s %s\n' "Qt6 kit" "$qt"; else printf '  [missing] %-20s %s\n' "Qt6 kit" "$qt"; missing=1; fi
    fi
    vc=$(sed -n "s/^VCPKG_INSTALLED_DIR:[A-Z]*=//p" "$cache" | tail -1)
    if [ -n "$vc" ]; then
        if [ -d "$vc/x64-windows" ]; then printf '  [ok] %-20s %s\n' "vcpkg installed" "$vc/x64-windows"; else printf '  [missing] %-20s %s\n' "vcpkg installed" "$vc/x64-windows"; missing=1; fi
    fi
    if [ "$missing" = 0 ]; then
        echo "doctor: no obvious holes"
        return 0
    fi
    echo "doctor: see [missing] rows above" >&2
    return 1
}

# ---------------------------------------------------------------------------
# configure / build / test / smoke
# ---------------------------------------------------------------------------
cmd_configure() {
    local build_dir="$repo_root/build-dev"
    local preset="" build_type="Debug" base_cache="" extra=()
    while [ $# -gt 0 ]; do
        case "$1" in
            --build-dir) build_dir=$2; shift 2 ;;
            --preset) preset=$2; shift 2 ;;
            --build-type) build_type=$2; shift 2 ;;
            --base-cache) base_cache=$2; shift 2 ;;
            --) shift; extra+=("$@"); break ;;
            -D*) extra+=("$1"); shift ;;
            *) fail "configure: unknown option: $1" ;;
        esac
    done
    local args=()
    if [ -n "$preset" ]; then
        args=(cmake --preset "$preset")
    else
        args=(cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type")
    fi
    # Seed only toolchain LOCATION variables from an existing configured tree —
    # every dependency is re-discovered fresh, so a stale cache can never pass
    # for a working configure. The vcpkg installed tree is reused read-only to
    # keep the gate offline (no manifest bootstrap, no downloads).
    if [ -n "$base_cache" ]; then
        [ -f "$base_cache" ] || fail "configure: base cache not found: $base_cache"
        local k v
        for k in CMAKE_GENERATOR CMAKE_MAKE_PROGRAM CMAKE_CXX_COMPILER CMAKE_C_COMPILER \
                 CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH VCPKG_INSTALLED_DIR VCPKG_TARGET_TRIPLET \
                 Qt6_DIR Qt6Keychain_DIR QCA_INCLUDE_DIR QCA_LIBRARY BISON_EXECUTABLE FLEX_EXECUTABLE; do
            v=$(sed -n "s/^$k:[A-Z]*=//p" "$base_cache" | tail -1)
            if [ -n "$v" ]; then args+=("-D$k=$v"); fi
        done
        args+=(-DVCPKG_MANIFEST_MODE=OFF)
        note "seeded toolchain locations from $base_cache (dependencies re-discovered fresh)"
    fi
    if [ ${#extra[@]} -gt 0 ]; then args+=("${extra[@]}"); fi
    run_logged configure "${args[@]}"
}

cmd_build() {
    local build_dir="$repo_root/build-dev"
    local jobs=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --build-dir) build_dir=$2; shift 2 ;;
            --jobs) jobs=$2; shift 2 ;;
            *) break ;;
        esac
    done
    if [ -z "$jobs" ]; then jobs=$(resolve_jobs); fi
    local cmd=(cmake --build "$build_dir" --parallel "$jobs")
    if [ $# -gt 0 ]; then cmd+=(-- "$@"); fi
    run_logged build "${cmd[@]}"
}

cmd_test() {
    local build_dir="$repo_root/build-dev"
    local regex=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --build-dir) build_dir=$2; shift 2 ;;
            -R) regex=$2; shift 2 ;;
            *) fail "test: unknown option: $1" ;;
        esac
    done
    local cmd=(ctest --test-dir "$build_dir" --output-on-failure -j1)
    if [ -n "$regex" ]; then cmd+=(-R "$regex"); fi
    QT_QPA_PLATFORM=offscreen run_logged test "${cmd[@]}"
}

cmd_smoke() {
    local build_dir="$repo_root/build-smoke"
    local base_cache=""
    local catch2_source=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --build-dir) build_dir=$2; shift 2 ;;
            --base-cache) base_cache=$2; shift 2 ;;
            --catch2-source) catch2_source=$2; shift 2 ;;
            *) fail "smoke: unknown option: $1" ;;
        esac
    done
    case "$build_dir" in
        /*) ;;
        *) build_dir="$repo_root/$build_dir" ;;
    esac
    if [ -d "$build_dir" ]; then
        note "removing stale build dir $build_dir (clean-tree gate)"
        rm -rf "$build_dir"
    fi
    mkdir -p "$build_dir" || fail "smoke: cannot create $build_dir"
    local seeded=()
    [ -n "$base_cache" ] && seeded+=(--base-cache "$base_cache")
    # Offline escape hatch: the repo's own Catch2 FetchContent (locked tag
    # v3.7.1) is the only network step in configure; point it at a prepared
    # clone so the gate never needs egress.
    [ -n "$catch2_source" ] && seeded+=(-DFETCHCONTENT_SOURCE_DIR_CATCH2="$catch2_source")
    if [ ${#seeded[@]} -gt 0 ]; then
        cmd_configure --build-dir "$build_dir" -- "${seeded[@]}" || {
            local elog="$(log_dir)/configure/run.log"
            if grep -qE "rc.*not found|系统找不到指定的文件|CMake Error at CMakeTestCXXCompiler|is not able to compile a simple test" "$elog" 2>/dev/null; then
                note "the compiler cannot link — the MSVC developer environment is missing."
                note "run from a Developer Prompt, or use the Windows wrapper (scripts\\windows\\build.cmd smoke),"
                note "which loads vcvars64 before configuring."
            fi
            return 1
        }
    else
        cmd_configure --build-dir "$build_dir" || return 1
    fi
    local log="$(log_dir)/configure/run.log"
    echo "smoke: clean-tree configure OK"
    echo "  build dir: $build_dir (started empty; no stale cache reused)"
    echo "  configure log: $log"
    if grep -q "SICNU dependency summary" "$log"; then
        echo "  dependency summary: present"
    else
        echo "  dependency summary: NOT printed yet (WP1 in progress)"
    fi
    return 0
}

cmd_preset_check() {
    # python3 on POSIX; many Windows setups only ship `python`.
    local py="${SICNU_PYTHON:-}"
    if [ -z "$py" ]; then
        if command -v python3 >/dev/null 2>&1; then py=python3
        elif command -v python >/dev/null 2>&1; then py=python
        else fail "preset-check: no Python interpreter found (set SICNU_PYTHON)"; fi
    fi
    "$py" "$script_dir/check_presets.py" "$repo_root/CMakePresets.json"
}

cmd_dep_fixture_test() {
    bash "$script_dir/dep_fixture_test.sh"
}

usage() {
    sed -n '3,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

# ---------------------------------------------------------------------------
# dispatch
# ---------------------------------------------------------------------------
case "${1:-}" in
    --print-jobs)
        # Hermetic diagnostic: prints the resolved job count (selftest hooks this).
        resolve_jobs "${SICNU_BUILD_JOBS:-}"
        ;;
    selftest) shift; cmd_selftest "$@" ;;
    doctor) shift; cmd_doctor "$@" ;;
    configure) shift; cmd_configure "$@" ;;
    build) shift; cmd_build "$@" ;;
    test) shift; cmd_test "$@" ;;
    smoke) shift; cmd_smoke "$@" ;;
    preset-check) shift; cmd_preset_check "$@" ;;
    dep-fixture-test) shift; cmd_dep_fixture_test "$@" ;;
    -h|--help|help) usage ;;
    *) usage >&2; exit 2 ;;
esac
