// test_portability_contract.cpp — cross-platform/GDAL-version compile
// contract (task A, Verification Platform 8.0).
//
// Guards the two post-7.0 master-breakage classes that were compile-shaped:
//
//   F1 (missing includes, #833): every leaf first-party header named in
//       tests/CMakeLists.txt's sicnu_add_header_probes() must compile as the
//       FIRST include of a cold TU — one generated probe executable per
//       header. This test file is the runtime twin: it includes the compat
//       seam first and asserts its selected configuration is coherent.
//
//   F2 (GDAL version drift, #834): the capability macros in
//       geospatial/util/gdal_compat.h must (a) match GDAL_VERSION_NUM,
//       (b) obey the version ordering (a newer capability implies every
//       older one), and (c) agree with the linked GDAL's reported version.
//
// Nothing here links QGIS/Qt — the whole suite builds in the light geo lane.
#include "geospatial/util/gdal_compat.h"

#include <catch2/catch_test_macros.hpp>

#include <gdal.h>

#include <string>

namespace
{
/// Capability -> minimum GDAL_VERSION_NUM (documented in gdal_compat.h).
struct CapabilityExpectation
{
    int macroValue;
    int minimumVersion;
};

bool versionImplies( int version, int minimum ) { return version >= minimum; }
} // namespace

TEST_CASE( "gdal_compat: macros reflect the compiled GDAL version",
           "[portability][gdal]" )
{
    // The header must re-export exactly the compiled version number — a
    // mismatch would mean the ladder and GDAL disagree about the world.
    REQUIRE( SICNU_GDAL_VERSION_NUM == GDAL_VERSION_NUM );

    // Cross-check against the runtime-reported version of the linked GDAL
    // (catches mismatched include/lib pairs, the nastiest portability trap).
    const std::string runtimeVersion = GDALVersionInfo( "VERSION_NUM" );
    const int runtimeNum = std::stoi( runtimeVersion );
    INFO( "compile-time GDAL_VERSION_NUM=" << GDAL_VERSION_NUM << " runtime="
          << runtimeVersion );
    REQUIRE( SICNU_GDAL_VERSION_NUM == runtimeNum );
}

TEST_CASE( "gdal_compat: capability ordering is coherent", "[portability][gdal]" )
{
    // Each capability implies every capability with a lower breakpoint.
    // Documented breakpoints: 3.5 (int64 types), 3.9 (RemoveHandler),
    // 3.10 (handle Err API), 3.12 (unique_ptr Open), 3.13 (byte Read/Write).
    const CapabilityExpectation expectations[] = {
        { SICNU_GDAL_VSI_REMOVE_HANDLER, GDAL_COMPUTE_VERSION( 3, 9, 0 ) },
        { SICNU_GDAL_VSI_HANDLE_ERR_API, GDAL_COMPUTE_VERSION( 3, 10, 0 ) },
        { SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR, GDAL_COMPUTE_VERSION( 3, 12, 0 ) },
        { SICNU_GDAL_VSI_HANDLE_READ_BYTES, GDAL_COMPUTE_VERSION( 3, 13, 0 ) },
        { SICNU_GDAL_INT64_DATATYPES, GDAL_COMPUTE_VERSION( 3, 5, 0 ) },
    };

    for ( const CapabilityExpectation &cap : expectations )
    {
        INFO( "minimumVersion=" << cap.minimumVersion );
        REQUIRE( cap.macroValue == versionImplies( GDAL_VERSION_NUM, cap.minimumVersion ) );
    }

    // Strict monotonic implications across the VSI ladder.
    if ( SICNU_GDAL_VSI_HANDLE_READ_BYTES )
    {
        REQUIRE( SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR );
        REQUIRE( SICNU_GDAL_VSI_HANDLE_ERR_API );
        REQUIRE( SICNU_GDAL_VSI_REMOVE_HANDLER );
    }
    if ( SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR )
    {
        REQUIRE( SICNU_GDAL_VSI_HANDLE_ERR_API );
        REQUIRE( SICNU_GDAL_VSI_REMOVE_HANDLER );
    }
    if ( SICNU_GDAL_VSI_HANDLE_ERR_API )
        REQUIRE( SICNU_GDAL_VSI_REMOVE_HANDLER );
}

TEST_CASE( "gdal_compat: range cache installs and uninstalls across the "
           "supported GDAL ladder",
           "[portability][gdal]" )
{
    // Behavioral twin of the #834 fix: the /vsirangecache/ VSI handler must
    // install, serve a stat/open error path (no network involved), and
    // uninstall cleanly on THIS GDAL version — whatever branch of the ladder
    // the compat seam selected. (Full cache semantics live in the io track's
    // suites; this contract only pins "the version-selected code loads and
    // tears down".)
    REQUIRE( sicnu::geo::RemoteRangeCache::installed() == false );
    sicnu::geo::RemoteRangeCache::install( sicnu::geo::RangeCacheConfig{} );
    REQUIRE( sicnu::geo::RemoteRangeCache::installed() );
    VSIStatBufL statBuf;
    // A non-cache path must simply not be handled by stat's cache branch and
    // report a normal filesystem error — the point is: no crash, no leak of
    // the version-selection into runtime behavior.
    const int statResult = VSIStatL( "/vsirangecache//definitely/not/a/cache/path", &statBuf );
    REQUIRE( statResult != 0 );
    sicnu::geo::RemoteRangeCache::uninstall();
    REQUIRE( sicnu::geo::RemoteRangeCache::installed() == false );
}
