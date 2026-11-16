// tests/test_r32_t04_change.cpp — WP09 (change-mask transaction finality),
// WP10 (spectral-index published claim data contract), WP19 (cartography
// adapter output declarations).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QTemporaryDir>

#include <gdal_priv.h>
#include <json/json.h>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/atomic_algorithm_registry.h"

using sicnu::operators::RSOperatorRegistry;

namespace
{
void makeFloatRaster( const QString &path, int w, int h, double base,
                      const std::array<double, 6> &gt )
{
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32,
                                  nullptr );
    REQUIRE( ds != nullptr );
    REQUIRE( GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) ) == CE_None );
    std::vector<float> data( static_cast<size_t>( w * h ) );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
            data[static_cast<size_t>( r * w + c )] =
                static_cast<float>( base + 5.0 * std::sin( 0.3 * r ) + ( c % 7 ) );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, w, h, data.data(), w,
                           h, GDT_Float32, 0, 0 )
                 == CE_None );
    GDALClose( ds );
}
} // namespace

// ===========================================================================
// WP09 — the PUBLISHED marker's claim is the FINAL mask. A crash between the
// magnitude publish and the mask write (simulated exactly: marker present,
// mask deleted) must self-heal on retry with committed tile reuse, not
// demand manual journal deletion (#2104).
// ===========================================================================
TEST_CASE( "WP09 marker-without-mask self-heals on retry with tile reuse",
           "[r32t04][wp09][change]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    GDALAllRegister();

    auto &registry = RSOperatorRegistry::instance();
    REQUIRE( registry.create( "rs:change_detection" ) != nullptr );

    constexpr int W = 48, H = 48;
    const std::array<double, 6> gt = { 500000.0, 30.0, 0.0, 4400000.0, 0.0, -30.0 };
    const QString before = tmp.path() + "/before.tif";
    const QString after = tmp.path() + "/after.tif";
    const QString output = tmp.path() + "/mask.tif";
    makeFloatRaster( before, W, H, 100.0, gt );
    makeFloatRaster( after, W, H, 100.0, gt );
    {
        GDALDatasetH ds = GDALOpen( after.toUtf8().constData(), GA_Update );
        std::vector<float> row( W, 500.0f );
        GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, 1, row.data(), W, 1,
                      GDT_Float32, 0, 0 );
        GDALClose( ds );
    }

    const auto runOnce = [&]( ) {
        auto op = registry.create( "rs:change_detection" );
        REQUIRE( op != nullptr );
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = output.toStdString();
        params["method"] = "difference";
        params["makeMask"] = true;
        params["thresholdMethod"] = "manual";
        params["threshold"] = 50.0;
        return op->run( params, ctx );
    };

    const Json::Value run1 = runOnce();
    REQUIRE( run1["changedPixels"].asUInt64() == static_cast<Json::UInt64>( W ) );

    // Simulate the crash window: the magnitude phase published (marker on
    // disk) but the final mask never landed.
    REQUIRE( QFile::remove( output ) );

    // Retry: must NOT demand manual journal deletion — the claim fails, the
    // marker demotes, committed magnitude tiles are REUSED (not recomputed),
    // and the mask is rebuilt byte-for-byte equivalently.
    const Json::Value run2 = runOnce();
    REQUIRE( run2["changedPixels"].asUInt64() == run1["changedPixels"].asUInt64() );
    REQUIRE( run2["thresholdUsed"].asDouble()
             == Catch::Approx( run1["thresholdUsed"].asDouble() ).epsilon( 1e-12 ) );
    REQUIRE( run2["tiles_reused"].asUInt64() == run2["total_tiles"].asUInt64() );

    // A third identical run is a clean cache hit again.
    const Json::Value run3 = runOnce();
    REQUIRE( run3["already_published"].asBool() );
    REQUIRE( run3["changedPixels"].asUInt64() == run1["changedPixels"].asUInt64() );
}

// ===========================================================================
// WP10 — an in-place edit of the published spectral-index artifact that
// preserves geometry + stamp (CRS rewrite through GDALUpdate) must demote
// the zero-work hit to a rebuild, never serve the edited artifact.
// ===========================================================================
TEST_CASE( "WP10 in-place CRS edit of published spectral output invalidates the claim",
           "[r32t04][wp10][spectral]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    GDALAllRegister();

    auto &registry = RSOperatorRegistry::instance();
    REQUIRE( registry.create( "rs:spectral_index" ) != nullptr );

    constexpr int W = 32, H = 32;
    const std::array<double, 6> gt = { 500000.0, 30.0, 0.0, 4400000.0, 0.0, -30.0 };
    const QString input = tmp.path() + "/multi.tif";
    const QString output = tmp.path() + "/ndvi.tif";
    {
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        GDALDatasetH ds = GDALCreate( driver, input.toUtf8().constData(), W, H, 2,
                                      GDT_Float32, nullptr );
        REQUIRE( ds != nullptr );
        REQUIRE( GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) ) == CE_None );
        REQUIRE( GDALSetProjection( ds, "EPSG:32650" ) == CE_None );
        for ( int band = 1; band <= 2; ++band )
        {
            std::vector<float> data( static_cast<size_t>( W * H ) );
            for ( size_t i = 0; i < data.size(); ++i )
                data[i] = band == 1 ? 100.f + ( i % 17 ) : 50.f + ( i % 13 );
            GDALRasterIO( GDALGetRasterBand( ds, band ), GF_Write, 0, 0, W, H, data.data(),
                          W, H, GDT_Float32, 0, 0 );
        }
        GDALClose( ds );
    }

    const auto runOnce = [&]() {
        auto op = registry.create( "rs:spectral_index" );
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["output"] = output.toStdString();
        params["index"] = "ndvi";
        params["bands"] = Json::Value( Json::arrayValue );
        params["bands"].append( "nir" );
        params["bands"].append( "red" );
        return op->run( params, ctx );
    };

    const Json::Value run1 = runOnce();
    REQUIRE( run1.isObject() );
    const Json::Value run2 = runOnce();
    REQUIRE( run2["already_published"].asBool() );

    // In-place edit preserving size/bands/stamp but rewriting the CRS.
    {
        GDALDatasetH ds = GDALOpen( output.toUtf8().constData(), GA_Update );
        REQUIRE( ds != nullptr );
        REQUIRE( GDALSetProjection( ds, "EPSG:4326" ) == CE_None );
        GDALClose( ds );
    }

    const Json::Value run3 = runOnce();
    REQUIRE_FALSE( run3["already_published"].asBool() ); // claim demoted → rebuild
    // The rebuilt artifact carries the run's declared CRS again.
    GDALDatasetH out = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( out != nullptr );
    const char *crs = GDALGetProjectionRef( out );
    REQUIRE( std::string( crs ).find( "32650" ) != std::string::npos );
    GDALClose( out );
}

// ===========================================================================
// WP19 — the six cartography operators declare their REAL outputs: report
// operators no longer synthesize a legacy single-RASTER-file port; the
// file-delivery operators keep an "output" VALUE port owned by their own
// atomic publish.
// ===========================================================================
TEST_CASE( "WP19 cartography adapters declare honest outputs (no phantom raster port)",
           "[r32t04][wp19][cartography]" )
{
    auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
    const struct
    {
        const char *algorithmId;
        bool producesFile;
    } expectations[] = {
        { "cartography:compose", false },  { "cartography:preflight", false },
        { "cartography:validate", false }, { "cartography:repair", false },
        { "cartography:export", false },   { "cartography:produce", false },
    };
    for ( const auto &expectation : expectations )
    {
        const auto adapter = registry.findAdapter( expectation.algorithmId );
        REQUIRE( adapter != nullptr );
        const auto descriptor = adapter->descriptor();
        REQUIRE( descriptor.outputs.size() > 0 );
        for ( const auto &port : descriptor.outputs )
        {
            INFO( "algorithm " << expectation.algorithmId << " port " << port.name );
            REQUIRE_FALSE( port.producesFile );
            REQUIRE_FALSE( port.type == sicnu::processing::DataType::Raster );
        }
    }
    // The delivery operators expose their chainable "output" value port.
    for ( const char *id : { "cartography:export", "cartography:produce" } )
    {
        const auto adapter = registry.findAdapter( id );
        bool hasOutputPort = false;
        for ( const auto &port : adapter->descriptor().outputs )
            if ( port.name == "output" )
                hasOutputPort = true;
        REQUIRE( hasOutputPort );
    }
}
