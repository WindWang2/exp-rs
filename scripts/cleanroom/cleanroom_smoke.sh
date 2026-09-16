#!/bin/sh
# cleanroom_smoke.sh — clean-machine simulation for the offline bundle (F19).
#
# Two modes, both zero-network by construction:
#
#   --mode env-starve (default, no privileges needed)
#       Runs the in-bundle verification and the env self-check under `env -i`
#       with a starved environment: no proxy vars, no repo-relative PATH, only
#       the bundle's bin/ plus the system interpreter the shipped verifier
#       declares. This is the closest a dev host gets to "unpacked on a clean
#       machine without the source tree".
#
#   --mode container
#       Same checks inside a fresh container (docker or podman) with no
#       network, mounting only the bundle (and nothing of the repo's build
#       tree). Requires a container runtime on the host.
#
# Prereq: a built bundle (scripts/build_offline_bundle.sh --build-dir ...),
# i.e. this tool verifies DEPLOYED state, it does not build anything.
# NOTE: point this at a PRISTINE bundle — the integrity contract covers every
# file at shipping time, so a bundle that already ran labs (outputs/ written)
# will flag those runtime artifacts as unlisted, by design. Re-assemble a
# fresh one if in doubt.
#
# usage: scripts/cleanroom/cleanroom_smoke.sh --bundle <dir> [--mode ...]
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mode="env-starve"
bundle=""
container_image="debian:bookworm-slim"

die() { echo "cleanroom_smoke: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --bundle) [ $# -ge 2 ] || die "--bundle needs a value"; bundle=$2; shift 2 ;;
    --mode)   [ $# -ge 2 ] || die "--mode needs a value"; mode=$2; shift 2 ;;
    --container-image) [ $# -ge 2 ] || die "--container-image needs a value"; container_image=$2; shift 2 ;;
    -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done
[ -d "$bundle" ] || die "bundle not found: $bundle"
[ -f "$bundle/manifest.json" ] || die "not a bundle (no manifest.json): $bundle"
[ "$mode" = "env-starve" ] || [ "$mode" = "container" ] || die "--mode must be env-starve or container"

bundle_abs=$(CDPATH= cd -- "$bundle" && pwd)

fail() { echo "CLEANROOM SMOKE FAIL: $*" >&2; exit 1; }

proxy_hygiene='-u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY -u all_proxy -u ftp_proxy -u FTP_PROXY'

if [ "$mode" = "env-starve" ]; then
  echo "== cleanroom smoke: env-starve mode (no proxy vars, minimal env) =="

  # 1. integrity via the SHIPPED verifier only (no repo access in PATH).
  env -i $proxy_hygiene PATH=/usr/bin:/bin \
    QT_QPA_PLATFORM=offscreen SICNU_OFFLINE=1 \
    /bin/sh "$bundle_abs/VERIFY.sh" > /tmp/cleanroom-verify.log 2>&1 \
    || { cat /tmp/cleanroom-verify.log; fail "in-bundle verify failed"; }
  tail -1 /tmp/cleanroom-verify.log

  # 2. environment self-check through the shipped CLI, still env-starved.
  cli="$bundle_abs/bin/sicnu_geo_rs_cli"
  [ -x "$cli" ] || fail "bundle has no bin/sicnu_geo_rs_cli (build it first)"
  rc=0
  env -i $proxy_hygiene PATH=/usr/bin:/bin HOME=/tmp \
    QT_QPA_PLATFORM=offscreen SICNU_OFFLINE=1 \
    "$cli" env-doctor > /tmp/cleanroom-env.log 2>&1 || rc=$?
  cat /tmp/cleanroom-env.log
  # exit 0 healthy; 2 means degraded/broken findings were reported — the
  # cleanroom harness REPORTS the verdict; it fails only when the doctor
  # itself could not run (exit > 2 would be a routing error).
  [ "$rc" = "0" ] || [ "$rc" = "2" ] || fail "env-doctor did not run cleanly (exit $rc)"
  grep -q "^ENV DOCTOR " /tmp/cleanroom-env.log || fail "no env-doctor verdict line"

  echo "CLEANROOM SMOKE PASS (env-starve): shipped verifier + env-doctor ran"
  echo "with no proxy env, no repo PATH, offline gate engaged."
  exit 0
fi

# ------------------------------------------------------------ container mode
echo "== cleanroom smoke: container mode (image: $container_image, no network) =="
runtime=""
for c in docker podman; do
  command -v "$c" >/dev/null 2>&1 && runtime=$c && break
done
[ -n "$runtime" ] || fail "no container runtime (docker/podman) on PATH"

# The image may lack python3; ship nothing extra — the verifier declares its
# interpreter. Use a runtime base that satisfies it, or fall back to running
# the host python against the mounted bundle for integrity only.
netflag="--network=none"
"$runtime" run --rm $netflag -v "$bundle_abs:/bundle:ro" "$container_image" \
  /bin/sh -c '/bundle/VERIFY.sh && echo CONTAINER VERIFY PASS' \
  > /tmp/cleanroom-container.log 2>&1 \
  || { cat /tmp/cleanroom-container.log; fail "container verify failed (image lacks python3? pass --container-image with python3)"; }
tail -2 /tmp/cleanroom-container.log

echo "CLEANROOM SMOKE PASS (container): in-bundle verify passed with no network."
echo "note: env-doctor inside the container needs the bundle's runtime closure;"
echo "on Linux portable bundles that means host libs the image may not have —"
echo "run env-starve mode for the full first-run story."
