// test_r24_a_nodata_semantics.cpp — R24-A science track (WP-A1, #1687).
//
// RED provenance: the equal-negative-pair cases below fail on
// origin/master@5fe642ec6 — bothNormalizable(-0.01f, -0.01f) returns false
// and normalizedDifference emits NaN for a mathematically well-defined
// pair (0/(-0.02) = 0). Post-atmospheric-correction scenes legitimately
// carry slight negative reflectance (dark water, shadow); the index
// kernels must not invalidate them (#1687).
//
// The sentinel-aware overloads and the validCount contract anchor are
// added in the GREEN step together with the fix; this file's legacy-API
// cases alone constitute the runtime RED evidence.

#include "processing/algorithms/math_utils.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <gdal_priv.h>
#include <json/json.h>

#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
using namespace sicnu::operators;

/// 4×1 two-band scene (band 1 = NIR, band 2 = Red) with declared NoData
/// −99 exercising every validity regime through the REAL rs:ndvi operator
/// path: an equal-negative reflectance pair, a normal pair, a declared
/// sentinel pixel, and an undeclared −9999 fill pixel.
bool writeNegativePairScene( const QString &path )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 4, 1, 2,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    const double gt[6] = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
    GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) != OGRERR_NONE )
    {
        GDALClose( ds );
        return false;
    }
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    GDALSetProjection( ds, wkt );
    CPLFree( wkt );
    const float nir[4] = { -0.01f, 0.3f, -99.0f, -9999.0f };
    const float red[4] = { -0.01f, 0.5f, 0.4f, 0.4f };
    for ( int b = 0; b < 2; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
        GDALSetRasterNoDataValue( band, -99.0 );
        const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 4, 1,
                                      const_cast<float *>( b == 0 ? nir : red ), 4, 1,
                                      GDT_Float32, 0, 0 ) == CE_None;
        if ( !ok )
        {
            GDALClose( ds );
            return false;
        }
    }
    GDALClose( ds );
    return true;
}

} // namespace

TEST_CASE( "bothNormalizable accepts legitimate equal-negative reflectance pairs",
           "[processing][math_utils][r24_a]" )
{
    // Post-correction dark water: both bands slightly negative and equal.
    // (a-b)/(a+b) = 0/(-0.02) = 0 — well-defined, must not be invalidated.
    CHECK( MathUtils::bothNormalizable( -0.01f, -0.01f ) );
    CHECK( MathUtils::bothNormalizable( -0.5f, -0.5f ) );
    // Distinct negatives were already valid; pin them so the fix cannot
    // regress the neighbouring behaviour.
    CHECK( MathUtils::bothNormalizable( -0.01f, -0.02f ) );
    CHECK( MathUtils::bothNormalizable( 0.3f, 0.5f ) );
}

TEST_CASE( "normalizedDifference evaluates equal-negative pairs to zero, not NaN",
           "[processing][math_utils][r24_a]" )
{
    const float a[] = { -0.01f, -0.02f, 0.3f, -0.5f };
    const float b[] = { -0.01f, -0.005f, 0.5f, -0.5f };
    float out[4] = { 1, 1, 1, 1 };
    REQUIRE( MathUtils::normalizedDifference( a, b, out, 4 ) );
    CHECK( out[0] == 0.0f );                            // RED on master: NaN
    CHECK( out[1] == Catch::Approx( 0.6f ).margin( 1e-6f ) ); // (-0.015)/(-0.025)
    CHECK( out[2] == Catch::Approx( -0.25f ).margin( 1e-6f ) );
    CHECK( out[3] == 0.0f );                            // RED on master: NaN
}

TEST_CASE( "normalizedDifference keeps the undeclared-DN sentinel net and NaN propagation",
           "[processing][math_utils][r24_a]" )
{
    const float a[] = { -9999.0f, 0.3f, kNan, 0.4f };
    const float b[] = { 0.3f, kNan, 0.5f, 0.6f };
    float out[4] = { 1, 1, 1, 1 };
    REQUIRE( MathUtils::normalizedDifference( a, b, out, 4 ) );
    CHECK( std::isnan( out[0] ) ); // undeclared -9999 fill stays invalid
    CHECK( std::isnan( out[1] ) ); // NaN operand propagates
    CHECK( std::isnan( out[2] ) );
    CHECK( out[3] == Catch::Approx( -0.2f ).margin( 1e-6f ) );
    // a == -b (denominator exactly zero) stays NaN via safeDiv.
    const float c[] = { 0.5f };
    const float d[] = { -0.5f };
    float o2[1] = { 1 };
    REQUIRE( MathUtils::normalizedDifference( c, d, o2, 1 ) );
    CHECK( std::isnan( o2[0] ) );
}

TEST_CASE( "normalizedDifference argument guards hold",
           "[processing][math_utils][r24_a]" )
{
    float a[1] = { 0.5f };
    float out[1] = { 1 };
    CHECK_FALSE( MathUtils::normalizedDifference( nullptr, a, out, 1 ) );
    CHECK_FALSE( MathUtils::normalizedDifference( a, nullptr, out, 1 ) );
    CHECK_FALSE( MathUtils::normalizedDifference( a, a, nullptr, 1 ) );
    CHECK_FALSE( MathUtils::normalizedDifference( a, a, out, 0 ) );
}

TEST_CASE( "sentinel-aware overloads route declared NoData exactly (#1687)",
           "[processing][math_utils][nodata][r24_a]" )
{
    constexpr float kSentinel = -99.0f; // declared, above the -999 net
    constexpr float kNone = std::numeric_limits<float>::quiet_NaN();

    CHECK( MathUtils::bothNormalizable( -99.0f, 0.5f, kSentinel, kNone ) == false );
    CHECK( MathUtils::bothNormalizable( 0.5f, -99.0f, kNone, kSentinel ) == false );
    // Equal negatives stay valid even when a sentinel is declared.
    CHECK( MathUtils::bothNormalizable( -0.01f, -0.01f, kSentinel, kSentinel ) );
    // An undeclared -9999 fill is still caught through the shared net.
    CHECK( MathUtils::bothNormalizable( -9999.0f, 0.5f, kSentinel, kNone ) == false );
    // A non-finite declared sentinel disables that side's exact check.
    CHECK( MathUtils::bothNormalizable( 0.3f, 0.5f, kNone, kNone ) );

    const float a[] = { -99.0f, -0.01f, 0.3f, -9999.0f };
    const float b[] = { 0.5f, -0.01f, 0.5f, 0.4f };
    float out[4] = { 1, 1, 1, 1 };
    REQUIRE( MathUtils::normalizedDifference( a, b, out, 4, kSentinel, kSentinel ) );
    CHECK( std::isnan( out[0] ) );  // declared sentinel → invalid
    CHECK( out[1] == 0.0f );        // equal negatives with sentinels → 0
    CHECK( out[2] == Catch::Approx( -0.25f ).margin( 1e-6f ) );
    CHECK( std::isnan( out[3] ) );  // undeclared fill via the shared net
    // Argument guards mirror the 4-argument form.
    CHECK_FALSE( MathUtils::normalizedDifference( a, b, out, 0, kSentinel, kSentinel ) );
    CHECK_FALSE( MathUtils::normalizedDifference( nullptr, b, out, 1, kSentinel, kSentinel ) );
}

TEST_CASE( "rs:ndvi operator path evaluates negative equal pairs, not NaN",
           "[operators][spectral_index][nodata][r24_a]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    REQUIRE( writeNegativePairScene( dir.filePath( "scene.tif" ) ) );

    auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
    REQUIRE( op != nullptr );
    RSOperatorContext context;
    Json::Value params( Json::objectValue );
    params["input"] = dir.filePath( "scene.tif" ).toStdString();
    params["output"] = dir.filePath( "ndvi.tif" ).toStdString();
    params["nir"] = 1;
    params["red"] = 2;
    REQUIRE_NOTHROW( op->run( params, context ) );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( dir.filePath( "ndvi.tif" ) ) );
    std::vector<float> ndvi( 4 );
    REQUIRE( ds.readBandData( 1, ndvi.data(), 4, 1 ) );
    // Equal-negative pair: NDVI = 0/(-0.02) = 0 (RED on master: NaN).
    CHECK( ndvi[0] == 0.0f );
    // Normal pair: (0.3-0.5)/(0.8) = -0.25.
    CHECK( ndvi[1] == Catch::Approx( -0.25f ).margin( 1e-6f ) );
    // Declared sentinel pixel stays NoData.
    CHECK( std::isnan( ndvi[2] ) );
    // Undeclared -9999 fill stays invalid via the legacy safety net.
    CHECK( std::isnan( ndvi[3] ) );
}
