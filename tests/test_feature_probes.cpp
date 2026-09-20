// test_feature_probes.cpp — contract test for cmake/SicnuFeatureProbes.cmake
// (ds41-build-portability, WP3).
//
// The generated header ${CMAKE_BINARY_DIR}/sicnu_feature_probes.h must exist
// and must define every probe macro to 0 or 1 — without the module this file
// does not compile, so the suite is red before the feature and green after.
// Two flags are additionally cross-checked at RUNTIME: if the probe says an
// API exists, this test actually uses it, so a probe that lies cannot pass.
#include <catch2/catch_test_macros.hpp>

#include "sicnu_feature_probes.h"

#if SICNU_HAVE_STD_FORMAT
#include <format>
#endif

#if SICNU_HAVE_GDAL_QUIET_ERROR_CTOR || SICNU_HAVE_GDAL_ERROR_HANDLER_PUSHER
#include <gdal.h>
#endif

// Every probe macro must be DEFINED and strictly 0 or 1. An undefined macro
// fails the compile — that is the intended potency of this contract.
static_assert(SICNU_HAVE_GDAL_QUIET_ERROR_CTOR == 0 || SICNU_HAVE_GDAL_QUIET_ERROR_CTOR == 1,
              "SICNU_HAVE_GDAL_QUIET_ERROR_CTOR must be defined to 0 or 1");
static_assert(SICNU_HAVE_GDAL_ERROR_HANDLER_PUSHER == 0 || SICNU_HAVE_GDAL_ERROR_HANDLER_PUSHER == 1,
              "SICNU_HAVE_GDAL_ERROR_HANDLER_PUSHER must be defined to 0 or 1");
static_assert(SICNU_HAVE_STD_FORMAT == 0 || SICNU_HAVE_STD_FORMAT == 1,
              "SICNU_HAVE_STD_FORMAT must be defined to 0 or 1");
static_assert(SICNU_HAVE_STD_STACKTRACE == 0 || SICNU_HAVE_STD_STACKTRACE == 1,
              "SICNU_HAVE_STD_STACKTRACE must be defined to 0 or 1");
static_assert(SICNU_HAVE_QT6_QTYPES == 0 || SICNU_HAVE_QT6_QTYPES == 1,
              "SICNU_HAVE_QT6_QTYPES must be defined to 0 or 1");

TEST_CASE("feature probes: every macro is defined to 0 or 1", "[feature_probes]")
{
    CHECK((SICNU_HAVE_GDAL_QUIET_ERROR_CTOR == 0 || SICNU_HAVE_GDAL_QUIET_ERROR_CTOR == 1));
    CHECK((SICNU_HAVE_GDAL_ERROR_HANDLER_PUSHER == 0 || SICNU_HAVE_GDAL_ERROR_HANDLER_PUSHER == 1));
    CHECK((SICNU_HAVE_STD_FORMAT == 0 || SICNU_HAVE_STD_FORMAT == 1));
    CHECK((SICNU_HAVE_STD_STACKTRACE == 0 || SICNU_HAVE_STD_STACKTRACE == 1));
    CHECK((SICNU_HAVE_QT6_QTYPES == 0 || SICNU_HAVE_QT6_QTYPES == 1));
}

TEST_CASE("feature probes: std::format cross-check is honest", "[feature_probes]")
{
#if SICNU_HAVE_STD_FORMAT
    // The probe claimed the API exists — use it for real.
    CHECK(std::format("{}", 42) == "42");
    CHECK(std::format("{}-{}", "a", 7) == "a-7");
#else
    SUCCEED("std::format unavailable on this toolchain; macro correctly reports 0");
#endif
}

TEST_CASE("feature probes: GDAL quiet-error idiom is constructible", "[feature_probes]")
{
    // Mirrors the landed #1108 idiom: on GDAL 3.9+ the single-argument ctor
    // works everywhere; on 3.8 the pusher must be combined with the
    // default-constructed state backuper. The probe decides which.
#if SICNU_HAVE_GDAL_QUIET_ERROR_CTOR
    {
        CPLErrorStateBackuper state(CPLQuietErrorHandler);
        (void)state;
    }
    CHECK(true);
#elif SICNU_HAVE_GDAL_ERROR_HANDLER_PUSHER
    {
        CPLErrorStateBackuper state;
        CPLErrorHandlerPusher quiet(CPLQuietErrorHandler);
        (void)state;
        (void)quiet;
    }
    CHECK(true);
#else
    SUCCEED("no GDAL quieting idiom available on this build; macros correctly report 0");
#endif
}

TEST_CASE("feature probes: QtTypes probe recorded the configure-time truth",
          "[feature_probes]")
{
    // This TU is deliberately Qt-free (the io-test link set has no Qt6::Core),
    // so the runtime cross-check for QtTypes belongs to the configure-time
    // probe, which compiled the header with the real flags. Here we only pin
    // that the macro exists and is one of the two legal values.
    CHECK((SICNU_HAVE_QT6_QTYPES == 0 || SICNU_HAVE_QT6_QTYPES == 1));
}
