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
# The cap itself is validated: an unvalidated override would silently break the
# advertised discipline (SICNU_BUILD_JOBS_CAP=99 --jobs 8 must not succeed) and
# a non-numeric one would make the arithmetic below fatal.
case "${SICNU_BUILD_JOBS_CAP:-}" in
    ''|*[!0-9]*) JOBS_CAP=2 ;;
    0) JOBS_CAP=1 ;;
    *) JOBS_CAP=${SICNU_BUILD_JOBS_CAP} ;;
esac

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
        0*) req=$JOBS_DEFAULT ;;  # leading zero: octal-looking input is not a job count
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
        ''|*[!0-9]*|0*) echo "$JOBS_DEFAULT" ;;
        *) if (( v > JOBS_CAP )); then echo "$JOBS_CAP"; else echo "$v"; fi ;;
    esac
}

# Clamp the inherited parallelism environment IN THE CURRENT SHELL and export
# it. Must not run inside a command substitution: an export made in a subshell
# dies with that subshell, and every tool that reads the variable (not just
# cmake --build) would see the unclamped value.
clamp_env_exports() {
    CMAKE_BUILD_PARALLEL_LEVEL=$(_clamp_env "${CMAKE_BUILD_PARALLEL_LEVEL:-}")
    CTEST_PARALLEL_LEVEL=$(_clamp_env "${CTEST_PARALLEL_LEVEL:-}")
    export CMAKE_BUILD_PARALLEL_LEVEL CTEST_PARALLEL_LEVEL
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
# Cross-process build lease (WP-G06).
#
# The -j<=2 cap bounds ONE build. It does nothing about several agents (or
# several worktrees) building on the same machine at the same time: each one
# stays inside its own cap and the machine still sees the sum, which is how a
# heavy C++/Qt/QGIS link step OOMs and takes the other lanes down with it.
#
# scripts/dev/resource_guard.py already implements the lease (exclusive lock
# file keyed on the build dir, dead-holder staleness, parallelism clamping) and
# is unit-tested — but nothing ever called it, so the lease was decorative.
# Every build/test payload below now runs inside it, sharing ONE lock per build
# directory with every other wrapper instance on this machine.
# ---------------------------------------------------------------------------
GUARD="$script_dir/dev/resource_guard.py"

# Echo the interpreter to run the guard with, or fail.
guard_python() {
    if [ -n "${SICNU_PYTHON:-}" ]; then printf '%s\n' "$SICNU_PYTHON"; return 0; fi
    if command -v python3 >/dev/null 2>&1; then printf 'python3\n'; return 0; fi
    if command -v python >/dev/null 2>&1; then printf 'python\n'; return 0; fi
    return 1
}

# guard_prefix <build-dir> <jobs> — echo the argv prefix that wraps a payload
# in the lease. Prints nothing (and explains why on stderr) when the lease is
# unavailable, so callers degrade to an ungoverned run *loudly*;
# SICNU_BUILD_REQUIRE_GUARD=1 turns that degradation into a refusal.
guard_prefix() {
    local build_dir=$1 jobs=$2 wait=${SICNU_BUILD_GUARD_WAIT:-1800}
    # R1-06: two contradictory switches must be an explicit conflict, not a
    # silent precedence that disarms the refusal.
    if [ -n "${SICNU_BUILD_REQUIRE_GUARD:-}" ] && [ -n "${SICNU_BUILD_NO_GUARD:-}" ]; then
        note "WARNING: SICNU_BUILD_REQUIRE_GUARD=1 and SICNU_BUILD_NO_GUARD are mutually exclusive"
        return 2
    fi
    if [ -n "${SICNU_BUILD_NO_GUARD:-}" ]; then
        note "WARNING: resource-guard disabled by SICNU_BUILD_NO_GUARD — running without the cross-process lease"
        return 0
    fi
    local py
    if [ ! -f "$GUARD" ] || ! py=$(guard_python); then
        note "WARNING: resource-guard unavailable ($GUARD / python) — running WITHOUT the cross-process lease"
        if [ -n "${SICNU_BUILD_REQUIRE_GUARD:-}" ]; then
            # R1-01: do NOT `fail` here. guard_prefix runs in a process
            # substitution, so exit only kills the subshell and the caller
            # would carry on ungoverned with exit 0. Return a status and let
            # the caller (this shell) refuse.
            return 2
        fi
        return 0
    fi
    printf '%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n' \
        "$py" "$GUARD" --lock-dir "$build_dir" --wait "$wait" \
        --parallel "$jobs" --max-parallel "$JOBS_CAP" --
}

# guard_prefix_into <array-name> <build-dir> <jobs> — fill the named array with
# the lease prefix. Returns 0 (prefix taken) or non-zero (the caller must
# refuse when SICNU_BUILD_REQUIRE_GUARD is set). Runs in the CURRENT shell, so
# the decision is observable in the script's exit code.
guard_prefix_into() {
    local __arr=$1; shift
    # Command substitution (not a temp file, not process substitution): the
    # exit status is observable here, in the shell that must decide, and there
    # is no temp file to clean up under a sandboxed `rm`.
    local out rc=0
    out=$(guard_prefix "$@") || rc=$?
    if [ "$rc" -ne 0 ]; then return "$rc"; fi
    local -n __dest=$__arr
    __dest=()
    if [ -n "$out" ]; then mapfile -t __dest <<<"$out"; fi
    return 0
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
    # Re-exec through $BASH, not "$0" directly: the file is 100644 in git, so
    # a plain exec of $0 fails with EACCES on a fresh clone.
    st_assert_eq 1 "$(env -u CMAKE_BUILD_PARALLEL_LEVEL -u CTEST_PARALLEL_LEVEL SICNU_BUILD_JOBS= "$BASH" "$0" --print-jobs)" "default jobs = 1"
    st_assert_eq 1 "$(SICNU_BUILD_JOBS=1 "$BASH" "$0" --print-jobs)" "jobs 1"
    st_assert_eq 2 "$(SICNU_BUILD_JOBS=2 "$BASH" "$0" --print-jobs)" "jobs 2 (cap)"
    local rc=0
    SICNU_BUILD_JOBS=3 "$BASH" "$0" --print-jobs >/dev/null 2>&1 || rc=$?
    st_assert_eq 2 "$rc" "jobs 3 refused with exit 2"
    rc=0
    SICNU_BUILD_JOBS=8 "$BASH" "$0" --print-jobs >/dev/null 2>&1 || rc=$?
    st_assert_eq 2 "$rc" "jobs 8 refused with exit 2"
    st_assert_eq 2 "$(SICNU_BUILD_JOBS=2 CMAKE_BUILD_PARALLEL_LEVEL=8 "$BASH" "$0" --print-jobs 2>/dev/null)" "inherited CMAKE_BUILD_PARALLEL_LEVEL=8 clamped to cap"
    st_assert_eq 1 "$(env -u CMAKE_BUILD_PARALLEL_LEVEL CTEST_PARALLEL_LEVEL=8 "$BASH" "$0" --print-jobs 2>/dev/null)" "inherited CTEST_PARALLEL_LEVEL clamped"

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
    if ( cd "$fixture" && "$BASH" "$script_dir/build.sh" --print-jobs >/dev/null 2>&1 ); then
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

    # --- 4. cross-process build lease (WP-G06) ------------------------------
    # Hermetic: a stub `cmake` proves whether the payload is reached, so no
    # toolchain is touched. The lease is the ONLY thing that serialises
    # several agents building on one host; the -j cap alone does not.
    local py=""
    py=$(guard_python 2>/dev/null) || py=""
    if [ -z "$py" ] || [ ! -f "$GUARD" ]; then
        echo "  SKIP: cross-process lease (no python or $GUARD)"
    else
        local lwork="$tmpbase/sicnu-lease-probe-$$"
        rm -rf "$lwork" 2>/dev/null || true
        mkdir -p "$lwork/build" "$lwork/bin" || { selftest_fail "cannot create lease fixture"; return 1; }
        printf '#!/usr/bin/env bash\necho PAYLOAD-RAN\nexit 0\n' > "$lwork/bin/cmake"
        chmod +x "$lwork/bin/cmake"
        # R1-07: wait for a marker written BY the payload, not for the lock
        # file's mere existence, and give it a 30s hold instead of 8s. A slow
        # python startup or a loaded machine can no longer race this probe into
        # a false result (the holder used to expire before the wrapper ran).
        local lmarker="$lwork/held.marker"
        "$py" "$GUARD" --lock-dir "$lwork/build" --wait 0 -- \
            "$py" -c "import sys, time; open(sys.argv[1], 'w').write('held'); time.sleep(30)" \
            "$lmarker" >/dev/null 2>&1 &
        local lholder=$!
        local lseen=0
        for _ in $(seq 1 300); do
            [ -f "$lmarker" ] && { lseen=1; break; }
            sleep 0.1
        done
        if [ "$lseen" != 1 ]; then
            selftest_fail "lease was never taken (probe setup broken)"
        else
            local lrc=0 lout
            lout=$(PATH="$lwork/bin:$PATH" SICNU_BUILD_GUARD_WAIT=0 \
                SICNU_BUILD_LOG_DIR="$lwork/logs" \
                "$0" build --build-dir "$lwork/build" 2>&1) || lrc=$?
            wait "$lholder" 2>/dev/null || true
            if printf '%s' "$lout" | grep -q "PAYLOAD-RAN"; then
                selftest_fail "build ran while another process held the lease"
            elif [ "$lrc" != 75 ]; then
                # Print what the wrapper actually said: without it, a refusal,
                # a missing interpreter and a broken fixture all look alike.
                selftest_fail "busy build dir must exit 75 (EX_TEMPFAIL), got $lrc"
                printf '%s\n' "$lout" | sed 's/^/      | /' >&2
            else
                echo "  ok: busy build dir refused (exit 75); payload never ran"
            fi
        fi
        rm -rf "$lwork" 2>/dev/null || true
    fi

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
    # R1-02: --jobs is a documented public flag and used to skip the cap check
    # entirely; resolve_jobs accepts the requested value, so pass it through.
    if [ -z "$jobs" ]; then jobs=$(resolve_jobs); else jobs=$(resolve_jobs "$jobs"); fi
    local cmd=(cmake --build "$build_dir" --parallel "$jobs")
    if [ $# -gt 0 ]; then cmd+=(-- "$@"); fi
    # WP-G06: queue on the shared lease instead of hammering the machine
    # alongside every other agent building on this host.
    # R1-01: guard_prefix runs in a process substitution, so a `fail` inside it
    # would only exit that subshell and the build would proceed ungoverned
    # with exit 0. Read the prefix back in THIS shell and decide here.
    local prefix=()
    guard_prefix_into prefix "$build_dir" "$jobs" \
        || fail "SICNU_BUILD_REQUIRE_GUARD=1 but the lease cannot be taken"
    if [ ${#prefix[@]} -gt 0 ]; then
        cmd=("${prefix[@]}" "${cmd[@]}")
    fi
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
    # WP-G06: ctest writes into the same build tree, so it takes the same lease
    # (a concurrent build must not be mutated under the test run).
    local prefix=()
    guard_prefix_into prefix "$build_dir" 1 \
        || fail "SICNU_BUILD_REQUIRE_GUARD=1 but the lease cannot be taken"
    if [ ${#prefix[@]} -gt 0 ]; then
        cmd=("${prefix[@]}" "${cmd[@]}")
    fi
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
    if grep -q "=== SICNU dependency summary ===" "$log"; then
        echo "  dependency summary: present"
    else
        echo "smoke: FAILED — the configure log has no '=== SICNU dependency summary ===' block" >&2
        echo "  (cmake/SicnuDepDoctor.cmake wiring missing? see $log)" >&2
        return 1
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
clamp_env_exports

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
