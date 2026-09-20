cmake_minimum_required(VERSION 3.20)
# Hermetic fixture for the configure-time dependency doctor
# (cmake/SicnuDepDoctor.cmake). Configurable through -DSICNU_FIXTURE_CASE:
#
#   missing  — a REQUIRED dependency is absent (CMAKE_DISABLE_FIND_PACKAGE_*):
#              configure must FAIL with per-platform install guidance, not a
#              bare find_package error.
#   stale    — the dependency exists but below the declared minimum: configure
#              must FAIL naming the minimum.
#   ok       — dependency satisfies the minimum: configure SUCCEEDS and prints
#              the dependency summary (negative control; proves the failure
#              cases above are about the doctor, not the fixture).
project(sicnu_dep_doctor_fixture LANGUAGES NONE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake")
include(SicnuDepDoctor)

if(SICNU_FIXTURE_CASE STREQUAL "missing")
    # GDAL is disabled by the driver via -DCMAKE_DISABLE_FIND_PACKAGE_GDAL=ON.
    sicnu_require_dependency(GDAL)
elseif(SICNU_FIXTURE_CASE STREQUAL "stale")
    # FakeDep ships version 1.0 in cmake/FakeDepConfig.cmake; asking for 2.0
    # must produce a version-mismatch diagnosis.
    sicnu_require_dependency(FakeDep MINIMUM_VERSION 2.0 CONFIG)
elseif(SICNU_FIXTURE_CASE STREQUAL "ok")
    sicnu_require_dependency(FakeDep MINIMUM_VERSION 1.0 CONFIG)
else()
    message(FATAL_ERROR "SICNU_FIXTURE_CASE must be missing|stale|ok (got '${SICNU_FIXTURE_CASE}')")
endif()

sicnu_dependency_summary()
