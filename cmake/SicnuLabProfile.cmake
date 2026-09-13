# cmake/SicnuLabProfile.cmake — teaching-lab build profile
# (D10 verification-baseline-green).
#
# Enabled with -DSICNU_LAB_PROFILE=ON at configure time. Default OFF: without
# the flag this file is a no-op and existing builds behave exactly as before.
#
# Purpose: undergraduate teaching machines (typically offline server-room
# hosts with modest CPUs) configure and build only what the labs need. The
# profile:
#   * drops optional heavy dependencies — vendored OTB/ITK, the ONNX Runtime
#     provider (its graceful-degradation stub stays), vendored GDAL, the
#     embedded Python console;
#   * removes the Python-bindings auto-detection path, whose pybind11
#     FetchContent is a NETWORK dependency lab machines cannot satisfy
#     (D10 evidence: the clone fails from the machine room);
#   * skips the verification suite — a teaching install does not build the
#     ~2000-target test registration; the standard build carries it.
# The lab operator set (raster/vector processing, cartography/composition,
# job engine, desktop GUI) is untouched: no operator or solver behaviour
# changes with this profile, only the target surface shrinks.
#
# Numbers of the measured configure/build delta land in the PR description
# and docs/verification/EVIDENCE (D10).

option(SICNU_LAB_PROFILE
  "Teaching lab profile: drop optional heavy deps, keep the lab operator set" OFF)

if(NOT SICNU_LAB_PROFILE)
  return()
endif()

message(STATUS "SICNU_LAB_PROFILE=ON — teaching-lab build profile "
               "(heavy optional deps off, verification suite off)")

# ── optional heavy dependencies: never build or fetch them here ────────────
set(SICNU_BUILD_OTB OFF CACHE BOOL
  "SICNU_LAB_PROFILE: no vendored OTB/ITK algorithm libraries" FORCE)
set(SICNU_WITH_ONNX_RUNTIME OFF CACHE BOOL
  "SICNU_LAB_PROFILE: ONNX Runtime provider stays a graceful stub" FORCE)
set(SICNU_VENDOR_GDAL OFF CACHE BOOL
  "SICNU_LAB_PROFILE: use the system GDAL, not the vendored chain" FORCE)
set(SICNU_EMBED_PYTHON OFF CACHE BOOL
  "SICNU_LAB_PROFILE: no embedded Python console" FORCE)
# ── Python bindings: opt-in detection pulls pybind11 over the network ──────
# src/operators/CMakeLists.txt honours this guard; lab machines must be able
# to configure with no egress at all.
set(SICNU_LAB_SKIP_PYTHON_BINDINGS ON)

# ── verification suite: not part of a teaching install ─────────────────────
# The full ladder/READINESS evidence comes from the standard build; the lab
# profile keeps only the shipped application and its operator set.
set(ENABLE_TESTS OFF CACHE BOOL
  "SICNU_LAB_PROFILE: teaching installs do not build the verification suite" FORCE)
