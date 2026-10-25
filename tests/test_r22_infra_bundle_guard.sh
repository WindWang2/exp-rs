#!/bin/sh
# test_r22_infra_bundle_guard.sh — #1697 regression oracle.
#
# build_offline_bundle.sh concatenates --version into the bundle path and
# rm -rf's it in non-incremental mode. A version carrying separators or
# traversal components must be REFUSED before any deletion happens, while
# legal versions (and --out paths containing spaces) must keep working.
#
# The sandbox mirrors only what the script touches before the guard-relevant
# stage: a stub build dir (the script only checks -x and copies the
# "binaries"), a repo skeleton with scripts/, and an out dir. The sandbox
# deliberately omits packaging/ and resources/ so a legal run stops
# deterministically (set -eu aborts at the fonts copy) right AFTER the
# rm/mkdir/cp stage the guard protects — nothing heavy (proj.db, manifests)
# ever runs.
#
# The no-deletion-outside---out property is pinned by the UNION of the
# script's two guards: if the version whitelist is removed, the traversal
# leg deletes $SANDBOX/victim and this test fails — that is the RED this
# suite pins. (The containment assertion is defense-in-depth behind the
# whitelist; the symlinked-out leg below pins that it never fires on legal
# input.)
set -eu

repo_root="."
while [ $# -gt 0 ]; do
  case "$1" in
    --repo-root) repo_root=$2; shift 2 ;;
    *) echo "test_r22_infra_bundle_guard: unknown arg: $1" >&2; exit 2 ;;
  esac
done

script_under_test="$repo_root/scripts/build_offline_bundle.sh"
[ -f "$script_under_test" ] || { echo "missing $script_under_test" >&2; exit 2; }

fail() { echo "test_r22_infra_bundle_guard: FAIL: $*" >&2; exit 1; }

sandbox=$(mktemp -d /tmp/r22-bundle-guard.XXXXXX)
trap 'rm -rf "$sandbox"' EXIT INT TERM

# --- sandbox layout --------------------------------------------------------
mkdir -p "$sandbox/repo/scripts" "$sandbox/build" "$sandbox/out" "$sandbox/victim"
cp "$script_under_test" "$sandbox/repo/scripts/build_offline_bundle.sh"
[ -f "$repo_root/scripts/verify_bundle_manifest.py" ] && \
  cp "$repo_root/scripts/verify_bundle_manifest.py" "$sandbox/repo/scripts/"
printf '#!/bin/sh\nexit 0\n' > "$sandbox/build/sicnu_geo_rs_cli"
printf '#!/bin/sh\nexit 0\n' > "$sandbox/build/sicnu_generate_samples"
chmod +x "$sandbox/build/sicnu_geo_rs_cli" "$sandbox/build/sicnu_generate_samples"
printf 'precious\n' > "$sandbox/victim/canary.txt"
# Pre-existing stale bundle proves the clean-assembly rm still happens for
# legal versions (the guard must not weaken the normal path).
mkdir -p "$sandbox/out/sicnu-lab-1.0"
printf 'stale\n' > "$sandbox/out/sicnu-lab-1.0/stale.txt"

run_bundle() { # run_bundle <version> [extra args...] -> rc in $rc, log in $log
  version=$1; shift
  log="$sandbox/run.log"
  rc=0
  sh "$sandbox/repo/scripts/build_offline_bundle.sh" \
     --build-dir "$sandbox/build" --out "$sandbox/out" \
     --version "$version" --skip-samples "$@" >"$log" 2>&1 || rc=$?
}

check_refused() { # the traversal battery: nonzero rc + explicit refusal + no deletion
  [ "$rc" -ne 0 ] || fail "version '$1' was accepted (rc=0)"
  grep -qi -e 'must be one path component' -e 'refusing' "$log" || \
    fail "version '$1' failed without the guard message; log: $(cat "$log")"
  [ -f "$sandbox/victim/canary.txt" ] || fail "version '$1' deleted the canary"
  [ ! -d "$sandbox/out/victim" ] || fail "version '$1' rebuilt a path inside out/"
}

# --- RED-pinned battery: every one of these used to rm -rf outside its bundle
for bad in '1.0/../../victim' '../..' '..' '.' '/etc' 'a/b' 'a\b' 'a b' \
           'a;touch-pwned' '$(touch pwned)' '`touch pwned`'; do
  run_bundle "$bad"
  check_refused "$bad"
done
[ -e "$sandbox/pwned" ] && fail "command substitution in version executed"
# Every battery leg must be refused BEFORE the rm stage, so the stale
# bundle prefix set up above must have survived the whole battery intact
# (R1-3: this is a real assertion, not a no-op).
[ -f "$sandbox/out/sicnu-lab-1.0/stale.txt" ] || \
  fail "a battery leg got past the guard and removed the stale bundle prefix"

# --- legal versions keep working (guard passes, clean rm preserved) --------
run_bundle "1.0"
[ "$rc" -ne 0 ] || fail "sandbox leg unexpectedly succeeded fully (layout drift?)"
[ -f "$sandbox/out/sicnu-lab-1.0/bin/sicnu_geo_rs_cli" ] || \
  fail "legal 1.0: CLI not copied into the fresh bundle"
[ ! -e "$sandbox/out/sicnu-lab-1.0/stale.txt" ] || \
  fail "legal 1.0: stale bundle content survived a clean (non-incremental) assembly"

run_bundle "v1.2_beta-3"
[ -f "$sandbox/out/sicnu-lab-v1.2_beta-3/bin/sicnu_geo_rs_cli" ] || \
  fail "legal v1.2_beta-3: bundle not assembled"

# --- --out containing spaces still works (oracle: bundle under out, intact) -
mkdir -p "$sandbox/out space"
log="$sandbox/run.log"; rc=0
sh "$sandbox/repo/scripts/build_offline_bundle.sh" \
   --build-dir "$sandbox/build" --out "$sandbox/out space" \
   --version "1.0" --skip-samples >"$log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] || fail "space-out leg unexpectedly succeeded fully (layout drift?)"
[ -f "$sandbox/out space/sicnu-lab-1.0/bin/sicnu_geo_rs_cli" ] || \
  fail "--out with spaces: guard or assembly broke on the space"
[ -d "$sandbox/out space/sicnu-lab-1.0/data" ] || \
  fail "--out with spaces: bundle skeleton incomplete"

# --- incremental: no rm of the existing bundle ------------------------------
printf 'keepme\n' > "$sandbox/out/sicnu-lab-1.0/keep.txt"
log="$sandbox/run.log"; rc=0
sh "$sandbox/repo/scripts/build_offline_bundle.sh" \
   --build-dir "$sandbox/build" --out "$sandbox/out" \
   --version "1.0" --skip-samples --incremental >"$log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] || fail "incremental leg unexpectedly succeeded fully (layout drift?)"
[ -f "$sandbox/out/sicnu-lab-1.0/keep.txt" ] || \
  fail "incremental: existing bundle content was deleted"

# --- symlinked --out: the containment check must not fire on legal input ----
# R1-7: realpath canonicalizes both sides, so an --out reached through a
# symlink still passes; a naive raw-string prefix compare would die here.
ln -s "$sandbox/out" "$sandbox/out-link"
log="$sandbox/run.log"; rc=0
sh "$sandbox/repo/scripts/build_offline_bundle.sh" \
   --build-dir "$sandbox/build" --out "$sandbox/out-link" \
   --version "1.0" --skip-samples >"$log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] || fail "symlink-out leg unexpectedly succeeded fully (layout drift?)"
grep -qi 'refusing to operate on bundle path outside' "$log" && \
  fail "containment check spuriously refused a legal symlinked --out"
[ -f "$sandbox/out/sicnu-lab-1.0/bin/sicnu_geo_rs_cli" ] || \
  fail "symlinked --out: bundle not assembled under the link target"

echo "test_r22_infra_bundle_guard: all legs passed"
