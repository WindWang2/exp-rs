// tests/test_temporal_algorithms.cpp — scientific correctness of the temporal
// operators against hand-computable synthetic datasets (goal §42–§50).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QDate>
#include <QDir>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>

#include <cmath>
#include <numeric>
#include <vector>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/algorithms/temporal/temporal_preflight.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;
using namespace sicnu::operators;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_temporal_algorithms";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

struct TestScene
{
    QString path;
    std::vector<float> values;
    int width = 2;
    int height = 2;
    std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
    QString date;
    bool declareNodata = true;
    double nodata = -9999.0;
    bool declareScale = false;
    double scale = 1.0;
    QString radiometricState;
    // extra bands: values per band (band 1 = values)
    std::vector<std::vector<float>> extraBands;
    std::vector<QByteArray> extraRoles;
};

bool writeTestScene( const TestScene &s )
{
    ensureGdalInit();
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) != OGRERR_NONE )
        return false;
    char *wktOut = nullptr;
    srs.exportToWkt( &wktOut );
    const QString wkt = QString::fromUtf8( wktOut );
    CPLFree( wktOut );

    const int bandCount = 1 + static_cast<int>( s.extraBands.size() );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( driver, s.path.toUtf8().constData(), s.width, s.height,
                                  bandCount, GDT_Float32, nullptr );
    if ( !ds )
        return false;
    GDALSetGeoTransform( ds, const_cast<double *>( s.gt.data() ) );
    GDALSetProjection( ds, wkt.toUtf8().constData() );
    if ( !s.radiometricState.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_RADIOMETRIC_STATE",
                             s.radiometricState.toUtf8().constData(), nullptr );
    if ( !s.date.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_ACQUISITION_DATE", s.date.toUtf8().constData(),
                             nullptr );

    auto writeBand = [&]( int bandIdx, const std::vector<float> &values, const QByteArray &role ) {
        GDALRasterBandH band = GDALGetRasterBand( ds, bandIdx );
        if ( s.declareNodata )
            GDALSetRasterNoDataValue( band, s.nodata );
        if ( s.declareScale )
        {
            GDALSetRasterScale( band, s.scale );
            GDALSetRasterOffset( band, 0.0 );
        }
        if ( !role.isEmpty() )
            GDALSetMetadataItem( band, "SICNU_BAND_ROLE", role.constData(), nullptr );
        return GDALRasterIO( band, GF_Write, 0, 0, s.width, s.height,
                             const_cast<float *>( values.data() ), s.width, s.height,
                             GDT_Float32, 0, 0 ) == CE_None;
    };

    bool ok = writeBand( 1, s.values, "red" );
    for ( size_t b = 0; ok && b < s.extraBands.size(); ++b )
        ok = writeBand( static_cast<int>( b + 2 ), s.extraBands.at( b ),
                        b < s.extraRoles.size() ? s.extraRoles.at( b ) : QByteArray() );
    GDALClose( ds );
    return ok;
}

TestScene makeTestScene( const QString &path, const QString &date,
                         const std::vector<float> &values, int width = 2, int height = 2 )
{
    TestScene s;
    s.path = path;
    s.width = width;
    s.height = height;
    s.values = values;
    s.date = date;
    return s;
}

std::unique_ptr<RSOperator> makeOp( const char *name )
{
    auto op = RSOperatorRegistry::instance().create( name );
    REQUIRE( op != nullptr );
    return op;
}

Json::Value runOp( const char *name, const Json::Value &params )
{
    auto op = makeOp( name );
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

/// Reads one band of a raster into a float vector.
std::vector<float> readBand( const QString &path, int band = 1 )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    REQUIRE( ds.readBandData( band, out.data(), ds.width(), ds.height() ) );
    return out;
}

struct Fixture
{
    QTemporaryDir dir;
    Fixture() { REQUIRE( dir.isValid() ); }
    QString filePath( const QString &name ) const { return dir.filePath( name ); }
};

} // namespace

// ------------------------------------------------------------- summary ----

TEST_CASE( "temporal_summary: hand-computed statistics", "[temporal][operators][summary]" )
{
    ensureApp();
    Fixture fx;

    // 2x2 grid, 3 dates: pixel series are hand-checkable (§42).
    // pixel(0,0): 10, 20, 30   -> mean 20, pop-std sqrt(200/3), min 10, max 30
    // pixel(1,0): 10, 10, 10   -> mean 10, std 0
    // pixel(0,1): -9999, 20, 30-> valid 2, mean 25
    // pixel(1,1): -9999 x3     -> valid 0 -> all NaN stats
    std::vector<float> d1 = { 10, 10, -9999, -9999 };
    std::vector<float> d2 = { 20, 10, 20, -9999 };
    std::vector<float> d3 = { 30, 10, 30, -9999 };
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "a.tif" ), QStringLiteral( "2025-01-01" ), d1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b.tif" ), QStringLiteral( "2025-01-11" ), d2 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "c.tif" ), QStringLiteral( "2025-01-21" ), d3 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "a.tif", "b.tif", "c.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "summary.tif" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_summary", params );
    REQUIRE( result["sceneCount"].asInt() == 3 );
    // valid samples: date1 2 + date2 3 + date3 3 = 8 of 12
    REQUIRE( result["validFraction"].asDouble() == Approx( 8.0 / 12.0 ) );

    const auto count = readBand( fx.filePath( QStringLiteral( "summary.tif" ) ), 2 );
    const auto mean = readBand( fx.filePath( QStringLiteral( "summary.tif" ) ), 3 );
    const auto minB = readBand( fx.filePath( QStringLiteral( "summary.tif" ) ), 4 );
    const auto maxB = readBand( fx.filePath( QStringLiteral( "summary.tif" ) ), 5 );
    const auto stdB = readBand( fx.filePath( QStringLiteral( "summary.tif" ) ), 6 );

    REQUIRE( count[0] == Approx( 3 ) );
    REQUIRE( mean[0] == Approx( 20.0 ) );
    REQUIRE( minB[0] == Approx( 10.0 ) );
    REQUIRE( maxB[0] == Approx( 30.0 ) );
    REQUIRE( stdB[0] == Approx( std::sqrt( 200.0 / 3.0 ) ) );

    REQUIRE( mean[1] == Approx( 10.0 ) );
    REQUIRE( stdB[1] == Approx( 0.0 ) );

    REQUIRE( count[2] == Approx( 2 ) );
    REQUIRE( mean[2] == Approx( 25.0 ) );

    REQUIRE( count[3] == Approx( 0 ) );
    REQUIRE( std::isnan( mean[3] ) );
    REQUIRE( std::isnan( minB[3] ) );
}

TEST_CASE( "temporal_summary: exact median with include_median", "[temporal][operators][summary]" )
{
    ensureApp();
    Fixture fx;
    // pixel 0 series: 10, 20, -9999 -> valid {10,20} -> median 15
    // pixel 1 series: -9999, 20, 30 -> valid {20,30} -> median 25
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "a.tif" ), QStringLiteral( "2025-01-01" ),
                                            { 10, -9999 } ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b.tif" ), QStringLiteral( "2025-01-02" ),
                                            { 20, 20 } ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "c.tif" ), QStringLiteral( "2025-01-03" ),
                                            { -9999, 30 } ) ) );
    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "a.tif", "b.tif", "c.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["include_median"] = true;
    params["output"] = fx.filePath( QStringLiteral( "summary.tif" ) ).toStdString();
    REQUIRE( runOp( "rs:temporal_summary", params )["sceneCount"].asInt() == 3 );

    const auto median = readBand( fx.filePath( QStringLiteral( "summary.tif" ) ), 7 );
    REQUIRE( median[0] == Approx( 15.0 ) ); // even valid count: avg of 10,20
    REQUIRE( median[1] == Approx( 25.0 ) ); // valid {20,30} -> even count -> 25
}

TEST_CASE( "temporal_summary: 100 dates stay memory-bounded", "[temporal][operators][summary][streaming]" )
{
    ensureApp();
    Fixture fx;
    QDir d( fx.dir.path() );
    d.mkdir( QStringLiteral( "dates" ) );
    Json::Value scenes( Json::arrayValue );
    for ( int i = 0; i < 100; ++i )
    {
        const QString p = fx.filePath( QStringLiteral( "dates/s%1.tif" ).arg( i ) );
        std::vector<float> values( 64, 100.0f + i );
        const QString date = QDate( 2024, 1, 1 ).addDays( i ).toString( Qt::ISODate );
        REQUIRE( writeTestScene( makeTestScene( p, date, values, 8, 8 ) ) );
        scenes.append( p.toStdString() );
    }
    Json::Value params( Json::objectValue );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["tile_size"] = 16; // force many tiles
    params["output"] = fx.filePath( QStringLiteral( "summary100.tif" ) ).toStdString();
    const Json::Value result = runOp( "rs:temporal_summary", params );
    REQUIRE( result["sceneCount"].asInt() == 100 );
    // working set must be tile-bounded, not 100 × full scene
    const std::uint64_t est = result["memory"]["workingSetEstimateBytes"].asUInt64();
    REQUIRE( est < 16ull * 1024 * 1024 ); // far below any full-scene cube
    const auto mean = readBand( fx.filePath( QStringLiteral( "summary100.tif" ) ), 3 );
    REQUIRE( mean.front() == Approx( 149.5 ) ); // mean of 100..199
}

// ----------------------------------------------------------- composite ----

TEST_CASE( "temporal_composite: cloudy/clear/invalid best-pixel (§47)", "[temporal][operators][composite]" )
{
    ensureApp();
    Fixture fx;
    // Date 1: cloudy pixel (value 100, masked by an explicit 0/1 mask band)
    // Date 2: clear (value 20)
    // Date 3: NoData
    {
        TestScene s = makeTestScene( fx.filePath( "cloudy.tif" ), QStringLiteral( "2025-06-01" ),
                                     { 100.0f } );
        s.width = 1;
        s.height = 1;
        s.extraBands = { { 1.0f } }; // explicit mask band: 1 = masked
        REQUIRE( writeTestScene( s ) );
    }
    {
        TestScene s = makeTestScene( fx.filePath( "clear.tif" ), QStringLiteral( "2025-06-11" ),
                                     { 20.0f } );
        s.width = 1;
        s.height = 1;
        s.extraBands = { { 0.0f } }; // 0 = clear
        REQUIRE( writeTestScene( s ) );
    }
    {
        TestScene s = makeTestScene( fx.filePath( "invalid.tif" ), QStringLiteral( "2025-06-21" ),
                                     { -9999.0f } );
        s.width = 1;
        s.height = 1;
        REQUIRE( writeTestScene( s ) );
    }

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    Json::Value cloudyEntry( Json::objectValue );
    cloudyEntry["path"] = fx.filePath( "cloudy.tif" ).toStdString();
    cloudyEntry["mask_band"] = 2;
    Json::Value clearEntry( Json::objectValue );
    clearEntry["path"] = fx.filePath( "clear.tif" ).toStdString();
    clearEntry["mask_band"] = 2;
    scenes.append( cloudyEntry );
    scenes.append( clearEntry );
    scenes.append( fx.filePath( "invalid.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "composite.tif" ) ).toStdString();
    const Json::Value result = runOp( "rs:temporal_composite", params );
    REQUIRE( result["sceneCount"].asInt() == 3 );

    const auto value = readBand( fx.filePath( QStringLiteral( "composite.tif" ) ), 1 );
    const auto count = readBand( fx.filePath( QStringLiteral( "composite.tif" ) ), 2 );
    // Date 1 masked by QA, Date 2 valid, Date 3 NoData -> best = Date 2
    REQUIRE( value[0] == Approx( 20.0f ) );
    REQUIRE( count[0] == Approx( 1.0f ) ); // observation count is honest (1 valid)
}

TEST_CASE( "temporal_composite: quality band beats temporal tie-break", "[temporal][operators][composite]" )
{
    ensureApp();
    Fixture fx;
    // Two valid dates; date 2 has higher quality score -> must win even though
    // date 1 is closer to the (midpoint) target.
    {
        TestScene s = makeTestScene( fx.filePath( "q1.tif" ), QStringLiteral( "2025-06-01" ), { 11.0f } );
        s.width = 1;
        s.height = 1;
        s.extraBands = { { 5.0f } }; // opt-in quality score band
        REQUIRE( writeTestScene( s ) );
    }
    {
        TestScene s = makeTestScene( fx.filePath( "q2.tif" ), QStringLiteral( "2025-06-21" ), { 22.0f } );
        s.width = 1;
        s.height = 1;
        s.extraBands = { { 9.0f } };
        REQUIRE( writeTestScene( s ) );
    }
    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    Json::Value s1( Json::objectValue );
    s1["path"] = fx.filePath( "q1.tif" ).toStdString();
    s1["quality_band"] = 2;
    Json::Value s2( Json::objectValue );
    s2["path"] = fx.filePath( "q2.tif" ).toStdString();
    s2["quality_band"] = 2;
    scenes.append( s1 );
    scenes.append( s2 );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "cq.tif" ) ).toStdString();
    runOp( "rs:temporal_composite", params );
    const auto value = readBand( fx.filePath( QStringLiteral( "cq.tif" ) ), 1 );
    const auto quality = readBand( fx.filePath( QStringLiteral( "cq.tif" ) ), 3 );
    REQUIRE( value[0] == Approx( 22.0f ) );
    REQUIRE( quality[0] == Approx( 9.0f ) );
}

TEST_CASE( "temporal_composite: period grouping produces per-period files", "[temporal][operators][composite]" )
{
    ensureApp();
    Fixture fx;
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "m1.tif" ), QStringLiteral( "2025-01-15" ), { 1.0f } ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "m2.tif" ), QStringLiteral( "2025-02-10" ), { 2.0f } ) ) );
    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "m1.tif" ).toStdString() );
    scenes.append( fx.filePath( "m2.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["period"] = "month";
    params["output"] = fx.filePath( QStringLiteral( "per_month.tif" ) ).toStdString();
    const Json::Value result = runOp( "rs:temporal_composite", params );
    REQUIRE( result["periodCount"].asInt() == 2 );
    REQUIRE( result["outputs"].size() == 2 );
    REQUIRE( QFile::exists( QString::fromStdString( result["outputs"][0]["output"].asString() ) ) );
    REQUIRE( QFile::exists( QString::fromStdString( result["outputs"][1]["output"].asString() ) ) );
}

TEST_CASE( "temporal masking honors product QA semantics (SCL / QA_PIXEL)",
           "[temporal][operators][masking]" )
{
    ensureApp();
    Fixture fx;

    SECTION( "Sentinel-2 SCL: cloud-high masked, vegetation kept" )
    {
        TestScene s = makeTestScene( fx.filePath( "scl.tif" ), QStringLiteral( "2025-06-01" ),
                                     { 100.0f, 20.0f } );
        s.width = 2;
        s.height = 1;
        s.extraBands = { { 9.0f, 4.0f } }; // 9 = cloud high, 4 = vegetation
        s.extraRoles = { QByteArrayLiteral( "scene_classification" ) };
        REQUIRE( writeTestScene( s ) );
        Json::Value params( Json::objectValue );
        Json::Value scenes( Json::arrayValue );
        scenes.append( fx.filePath( "scl.tif" ).toStdString() );
        params["scenes"] = scenes;
        params["band"] = 1;
        params["output"] = fx.filePath( QStringLiteral( "scl_out.tif" ) ).toStdString();
        runOp( "rs:temporal_summary", params );
        const auto valid = readBand( fx.filePath( QStringLiteral( "scl_out.tif" ) ), 2 );
        REQUIRE( valid[0] == Approx( 0 ) ); // cloud-high masked
        REQUIRE( valid[1] == Approx( 1 ) ); // vegetation kept
    }
    SECTION( "Landsat QA_PIXEL named band: cloud bit masked, clear bit kept" )
    {
        TestScene s = makeTestScene( fx.filePath( "qa.tif" ), QStringLiteral( "2025-06-01" ),
                                     { 100.0f, 20.0f } );
        s.width = 2;
        s.height = 1;
        s.extraBands = { { 8.0f, 64.0f } }; // 8 = cloud (bit 3), 64 = clear (bit 6)
        REQUIRE( writeTestScene( s ) );
        // name the band like a Landsat QA_PIXEL layer so the kind is inferred
        {
            GDALDatasetH ds = GDALOpen( fx.filePath( "qa.tif" ).toUtf8().constData(), GA_Update );
            REQUIRE( ds != nullptr );
            GDALSetDescription( GDALGetRasterBand( ds, 2 ), "QA_PIXEL" );
            GDALClose( ds );
        }
        Json::Value params( Json::objectValue );
        Json::Value scenes( Json::arrayValue );
        scenes.append( fx.filePath( "qa.tif" ).toStdString() );
        params["scenes"] = scenes;
        params["band"] = 1;
        params["output"] = fx.filePath( QStringLiteral( "qa_out.tif" ) ).toStdString();
        runOp( "rs:temporal_summary", params );
        const auto valid = readBand( fx.filePath( QStringLiteral( "qa_out.tif" ) ), 2 );
        REQUIRE( valid[0] == Approx( 0 ) ); // cloud bit masked
        REQUIRE( valid[1] == Approx( 1 ) ); // clear pixel kept
    }
}

// -------------------------------------------------------- index series ----

TEST_CASE( "temporal_index_series reuses the single-scene kernel bit-for-bit (§19)",
           "[temporal][operators][index]" )
{
    ensureApp();
    Fixture fx;
    // 3-band scene: red=300, nir=500 -> NDVI = (500-300)/(500+300) = 0.25
    TestScene s = makeTestScene( fx.filePath( "ndvi_scene.tif" ),
                                 QStringLiteral( "2025-04-01" ), { 300.0f, 300.0f, 300.0f, 300.0f } );
    s.extraBands = { { 500.0f, 500.0f, 500.0f, 500.0f }, { 100.0f, 100.0f, 100.0f, 100.0f } };
    s.extraRoles = { QByteArrayLiteral( "nir" ), QByteArrayLiteral( "blue" ) };
    REQUIRE( writeTestScene( s ) );

    Json::Value seriesParams( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "ndvi_scene.tif" ).toStdString() );
    seriesParams["scenes"] = scenes;
    seriesParams["index"] = "NDVI";
    seriesParams["output"] = fx.filePath( QStringLiteral( "ndvi_series.tif" ) ).toStdString();
    const Json::Value seriesResult = runOp( "rs:temporal_index_series", seriesParams );
    REQUIRE( seriesResult["sceneCount"].asInt() == 1 );

    Json::Value singleParams( Json::objectValue );
    singleParams["input"] = fx.filePath( "ndvi_scene.tif" ).toStdString();
    singleParams["index"] = "NDVI";
    singleParams["red"] = 1;
    singleParams["nir"] = 2;
    singleParams["output"] = fx.filePath( QStringLiteral( "ndvi_single.tif" ) ).toStdString();
    runOp( "rs:spectral_index", singleParams );

    const auto seriesBand = readBand( fx.filePath( QStringLiteral( "ndvi_series.tif" ) ), 1 );
    const auto singleBand = readBand( fx.filePath( QStringLiteral( "ndvi_single.tif" ) ), 1 );
    REQUIRE( seriesBand.size() == singleBand.size() );
    for ( size_t i = 0; i < seriesBand.size(); ++i )
    {
        if ( std::isnan( singleBand[i] ) )
            REQUIRE( std::isnan( seriesBand[i] ) );
        else
            REQUIRE( seriesBand[i] == singleBand[i] ); // bit-identical: same kernel
    }
    REQUIRE( seriesBand[0] == Approx( 0.25f ) );
}

TEST_CASE( "temporal_index_series keeps acquisition metadata per band", "[temporal][operators][index]" )
{
    ensureApp();
    Fixture fx;
    TestScene s = makeTestScene( fx.filePath( "ndvi2.tif" ), QStringLiteral( "2025-04-03" ),
                                 { 300.0f, 300.0f } );
    s.width = 2;
    s.height = 1;
    s.extraBands = { { 500.0f, 500.0f } };
    s.extraRoles = { QByteArrayLiteral( "nir" ) };
    REQUIRE( writeTestScene( s ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "ndvi2.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["index"] = "NDVI";
    params["output"] = fx.filePath( QStringLiteral( "ndvi2_out.tif" ) ).toStdString();
    runOp( "rs:temporal_index_series", params );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( fx.filePath( QStringLiteral( "ndvi2_out.tif" ) ) ) );
    REQUIRE( ds.bandMetadataItem( 1, "SICNU_ACQUISITION_DATE" ) == QStringLiteral( "2025-04-03" ) );
}

// --------------------------------------------------------------- trend ----

TEST_CASE( "temporal_trend: irregular real time intervals (§43)", "[temporal][operators][trend]" )
{
    ensureApp();
    Fixture fx;
    // Single pixel series at day 0/2/10 (hand-checkable): y = 1 + 0.5 t
    // slope 0.5/day, intercept 1, R² 1.
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "t0.tif" ), QStringLiteral( "2025-01-01" ), { 1.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "t2.tif" ), QStringLiteral( "2025-01-03" ), { 2.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "t10.tif" ), QStringLiteral( "2025-01-11" ), { 6.0f }, 1, 1 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "t0.tif" ).toStdString() );
    scenes.append( fx.filePath( "t2.tif" ).toStdString() );
    scenes.append( fx.filePath( "t10.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "trend.tif" ) ).toStdString();
    runOp( "rs:temporal_trend", params );

    const auto slope = readBand( fx.filePath( QStringLiteral( "trend.tif" ) ), 1 );
    const auto intercept = readBand( fx.filePath( QStringLiteral( "trend.tif" ) ), 2 );
    const auto r2 = readBand( fx.filePath( QStringLiteral( "trend.tif" ) ), 3 );
    const auto n = readBand( fx.filePath( QStringLiteral( "trend.tif" ) ), 4 );
    REQUIRE( slope[0] == Approx( 0.5 ) ); // NOT 2.5 (index-as-time bug)
    REQUIRE( intercept[0] == Approx( 1.0 ) );
    REQUIRE( r2[0] == Approx( 1.0 ) );
    REQUIRE( n[0] == Approx( 3 ) );
}

TEST_CASE( "temporal_trend: partially valid series and NoData propagation", "[temporal][operators][trend]" )
{
    ensureApp();
    Fixture fx;
    // day 0: 0.0 (valid), day 2: nodata, day 4: 1.0 -> slope 0.25/day over valid only
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "p0.tif" ), QStringLiteral( "2025-01-01" ), { 0.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "p2.tif" ), QStringLiteral( "2025-01-03" ), { -9999.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "p4.tif" ), QStringLiteral( "2025-01-05" ), { 1.0f }, 1, 1 ) ) );
    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "p0.tif", "p2.tif", "p4.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "trend2.tif" ) ).toStdString();
    runOp( "rs:temporal_trend", params );
    const auto slope = readBand( fx.filePath( QStringLiteral( "trend2.tif" ) ), 1 );
    const auto n = readBand( fx.filePath( QStringLiteral( "trend2.tif" ) ), 4 );
    REQUIRE( slope[0] == Approx( 0.25 ) );
    REQUIRE( n[0] == Approx( 2 ) );
}

// ------------------------------------------------------------- anomaly ----

TEST_CASE( "temporal_anomaly: z-score against known baseline (§24)", "[temporal][operators][anomaly]" )
{
    ensureApp();
    Fixture fx;
    // baseline: 10, 20, 30 (sample std 10, mean 20); target: 40 -> z = 2
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b1.tif" ), QStringLiteral( "2025-01-01" ), { 10.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b2.tif" ), QStringLiteral( "2025-01-02" ), { 20.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b3.tif" ), QStringLiteral( "2025-01-03" ), { 30.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "tg.tif" ), QStringLiteral( "2025-02-01" ), { 40.0f }, 1, 1 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "b1.tif", "b2.tif", "b3.tif", "tg.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["method"] = "zscore";
    params["target_time"] = "2025-02-01";
    params["output"] = fx.filePath( QStringLiteral( "anomaly.tif" ) ).toStdString();
    runOp( "rs:temporal_anomaly", params );

    const auto z = readBand( fx.filePath( QStringLiteral( "anomaly.tif" ) ), 1 );
    const auto baseMean = readBand( fx.filePath( QStringLiteral( "anomaly.tif" ) ), 2 );
    const auto baseN = readBand( fx.filePath( QStringLiteral( "anomaly.tif" ) ), 3 );
    REQUIRE( baseMean[0] == Approx( 20.0 ) );
    REQUIRE( baseN[0] == Approx( 3 ) );
    REQUIRE( z[0] == Approx( 2.0 ) );
}

TEST_CASE( "temporal_anomaly: degenerate baselines are NoData, not wrong numbers", "[temporal][operators][anomaly]" )
{
    ensureApp();
    Fixture fx;
    SECTION( "stddev == 0 gives NaN z-score but a defined difference" )
    {
        REQUIRE( writeTestScene( makeTestScene( fx.filePath( "c1.tif" ), QStringLiteral( "2025-01-01" ), { 5.0f }, 1, 1 ) ) );
        REQUIRE( writeTestScene( makeTestScene( fx.filePath( "c2.tif" ), QStringLiteral( "2025-01-02" ), { 5.0f }, 1, 1 ) ) );
        REQUIRE( writeTestScene( makeTestScene( fx.filePath( "ct.tif" ), QStringLiteral( "2025-02-01" ), { 9.0f }, 1, 1 ) ) );
        Json::Value params( Json::objectValue );
        Json::Value scenes( Json::arrayValue );
        for ( const char *p : { "c1.tif", "c2.tif", "ct.tif" } )
            scenes.append( fx.filePath( p ).toStdString() );
        params["scenes"] = scenes;
        params["band"] = 1;
        params["target_time"] = "2025-02-01";

        params["method"] = "zscore";
        params["output"] = fx.filePath( QStringLiteral( "z.tif" ) ).toStdString();
        runOp( "rs:temporal_anomaly", params );
        REQUIRE( std::isnan( readBand( fx.filePath( QStringLiteral( "z.tif" ) ) )[0] ) );

        params["method"] = "difference";
        params["output"] = fx.filePath( QStringLiteral( "d.tif" ) ).toStdString();
        runOp( "rs:temporal_anomaly", params );
        REQUIRE( readBand( fx.filePath( QStringLiteral( "d.tif" ) ) )[0] == Approx( 4.0 ) );
    }
    SECTION( "insufficient baseline observations yield NoData" )
    {
        REQUIRE( writeTestScene( makeTestScene( fx.filePath( "o1.tif" ), QStringLiteral( "2025-01-01" ), { 5.0f }, 1, 1 ) ) );
        REQUIRE( writeTestScene( makeTestScene( fx.filePath( "o2.tif" ), QStringLiteral( "2025-02-01" ), { 8.0f }, 1, 1 ) ) );
        Json::Value params( Json::objectValue );
        Json::Value scenes( Json::arrayValue );
        scenes.append( fx.filePath( "o1.tif" ).toStdString() );
        scenes.append( fx.filePath( "o2.tif" ).toStdString() );
        params["scenes"] = scenes;
        params["band"] = 1;
        params["target_time"] = "2025-02-01";
        params["method"] = "zscore";
        params["min_observations"] = 5;
        params["output"] = fx.filePath( QStringLiteral( "o.tif" ) ).toStdString();
        const Json::Value result = runOp( "rs:temporal_anomaly", params );
        REQUIRE( result["baselineInsufficient"].asBool() );
        REQUIRE( std::isnan( readBand( fx.filePath( QStringLiteral( "o.tif" ) ) )[0] ) );
    }
}

// ------------------------------------------------------- extract series ----

TEST_CASE( "temporal_extract_series: point series", "[temporal][operators][extract]" )
{
    ensureApp();
    Fixture fx;
    // 2x2 grid; point at center of pixel (1,1): map = origin + 1.5*30, y = 4500000 - 1.5*30
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "e1.tif" ), QStringLiteral( "2025-01-01" ),
                                            { 1, 2, 3, 4 } ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "e2.tif" ), QStringLiteral( "2025-01-02" ),
                                            { 10, 20, 30, -9999 } ) ) );
    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "e1.tif" ).toStdString() );
    scenes.append( fx.filePath( "e2.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    Json::Value point( Json::arrayValue );
    point.append( 500000 + 1.5 * 30 );
    point.append( 4500000 - 1.5 * 30 );
    params["point"] = point;
    params["output"] = fx.filePath( QStringLiteral( "point.csv" ) ).toStdString();
    const Json::Value result = runOp( "rs:temporal_extract_series", params );
    REQUIRE( result["series"].size() == 2 );
    REQUIRE( result["series"][0]["value"].asDouble() == Approx( 4.0 ) ); // pixel (1,1)
    REQUIRE( result["series"][1]["valid"].asBool() == false );           // NoData
    REQUIRE( QFile::exists( fx.filePath( QStringLiteral( "point.csv" ) ) ) );
}

TEST_CASE( "temporal_extract_series: ROI polygon bounded by bbox (§26)", "[temporal][operators][extract]" )
{
    ensureApp();
    Fixture fx;
    // 4x4 grid of value 5..; polygon covering only pixel centers (0,0) and (1,0)
    std::vector<float> values( 16 );
    std::iota( values.begin(), values.end(), 1.0f );
    TestScene s = makeTestScene( fx.filePath( "roi.tif" ), QStringLiteral( "2025-01-01" ), values, 4, 4 );
    REQUIRE( writeTestScene( s ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "roi.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    // rectangle containing row-0 pixel centers (500015,4499985) and
    // (500045,4499985) but excluding rows 1-3 (center y = 4499955...)
    Json::Value polygon( Json::arrayValue );
    auto vertex = []( double x, double y ) {
        Json::Value v( Json::arrayValue );
        v.append( x );
        v.append( y );
        return v;
    };
    polygon.append( vertex( 500005, 4500005 ) );
    polygon.append( vertex( 500060, 4500005 ) );
    polygon.append( vertex( 500060, 4499975 ) );
    polygon.append( vertex( 500005, 4499975 ) );
    polygon.append( vertex( 500005, 4500005 ) );
    params["polygon"] = polygon;
    params["output"] = fx.filePath( QStringLiteral( "roi.csv" ) ).toStdString();
    const Json::Value result = runOp( "rs:temporal_extract_series", params );
    REQUIRE( result["mode"].asString() == "roi" );
    REQUIRE( result["series"].size() == 1 );
    // The triangle contains pixel centers (0,0)=1 and (1,0)=2 -> mean 1.5
    REQUIRE( result["series"][0]["valid_count"].asUInt64() == 2 );
    REQUIRE( result["series"][0]["mean"].asDouble() == Approx( 1.5 ) );
    REQUIRE( result["series"][0]["min"].asDouble() == Approx( 1.0 ) );
    REQUIRE( result["series"][0]["max"].asDouble() == Approx( 2.0 ) );
}

// ------------------------------------------------------ cross-cutting ----

TEST_CASE( "temporal operators: scale/offset float vs scaled-int agree (§45)",
           "[temporal][operators][scale]" )
{
    ensureApp();
    Fixture fx;
    // float 0.1/0.2/0.3 vs int 1000/2000/3000 with declared scale 1e-4: the
    // explicit-declared normalization must make results identical.
    auto runSummary = [&]( const std::vector<float> &vals, bool declareScale, const QString &tag,
                           const QString &out ) {
        std::vector<TestScene> scenes;
        const char *dates[] = { "2025-01-01", "2025-01-11", "2025-01-21" };
        for ( int i = 0; i < 3; ++i )
        {
            TestScene s = makeTestScene( fx.filePath( QStringLiteral( "sc_%1_%2.tif" ).arg( tag ).arg( i ) ),
                                         QString::fromLatin1( dates[i] ), { vals[i] }, 1, 1 );
            s.declareScale = declareScale;
            s.scale = 0.0001;
            REQUIRE( writeTestScene( s ) );
        }
        Json::Value params( Json::objectValue );
        Json::Value sceneArr( Json::arrayValue );
        for ( int i = 0; i < 3; ++i )
            sceneArr.append(
                fx.filePath( QStringLiteral( "sc_%1_%2.tif" ).arg( tag ).arg( i ) ).toStdString() );
        params["scenes"] = sceneArr;
        params["band"] = 1;
        params["output"] = fx.filePath( out ).toStdString();
        runOp( "rs:temporal_summary", params );
        return readBand( fx.filePath( out ), 3 )[0]; // mean band
    };
    const float floatMean =
        runSummary( { 0.1f, 0.2f, 0.3f }, false, QStringLiteral( "f" ), QStringLiteral( "f.tif" ) );
    const float scaledMean = runSummary( { 1000.0f, 2000.0f, 3000.0f }, true, QStringLiteral( "s" ),
                                         QStringLiteral( "s.tif" ) );
    REQUIRE( floatMean == Approx( 0.2f ) );
    REQUIRE( scaledMean == Approx( floatMean ).epsilon( 1e-6 ) );
}

TEST_CASE( "temporal operators: cancellation leaves no partial output (§49)",
           "[temporal][operators][cancel]" )
{
    ensureApp();
    Fixture fx;
    // Larger grid so the run spans multiple tiles before cancellation.
    QDir d( fx.dir.path() );
    d.mkdir( QStringLiteral( "cancel" ) );
    Json::Value scenes( Json::arrayValue );
    for ( int i = 0; i < 4; ++i )
    {
        const QString p = fx.filePath( QStringLiteral( "cancel/s%1.tif" ).arg( i ) );
        std::vector<float> values( 64 * 64, 1.0f + i );
        REQUIRE( writeTestScene(
            makeTestScene( p, QStringLiteral( "2025-01-%1" ).arg( i + 1, 2, 10, QChar( '0' ) ),
                           values, 64, 64 ) ) );
        scenes.append( p.toStdString() );
    }
    const QString outPath = fx.filePath( QStringLiteral( "cancelled.tif" ) );

    auto op = makeOp( "rs:temporal_summary" );
    RSOperatorContext ctx;
    std::atomic<bool> cancelFlag{ false };
    ctx.setCancelFlag( &cancelFlag );
    // flip the flag after the first progress report (tile 1 of many)
    int reports = 0;
    ctx.setProgressCallback( [&]( double, const std::string & ) {
        if ( ++reports >= 1 )
            cancelFlag.store( true );
    } );

    Json::Value params( Json::objectValue );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["tile_size"] = 16; // many tiles -> cancellation lands mid-run
    params["output"] = outPath.toStdString();

    bool threw = false;
    try
    {
        op->run( params, ctx );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        REQUIRE( e.code() == ErrorCode::Cancelled );
    }
    REQUIRE( threw );
    REQUIRE( !QFile::exists( outPath ) ); // no half-written success-looking file
}

TEST_CASE( "temporal operators: mixed radiometric states rejected before compute (§10)",
           "[temporal][operators][preflight]" )
{
    ensureApp();
    Fixture fx;
    TestScene a = makeTestScene( fx.filePath( "sr.tif" ), QStringLiteral( "2025-01-01" ), { 0.1f }, 1, 1 );
    a.radiometricState = QStringLiteral( "surface_reflectance" );
    TestScene b = makeTestScene( fx.filePath( "dn.tif" ), QStringLiteral( "2025-01-02" ), { 1000.0f }, 1, 1 );
    b.radiometricState = QStringLiteral( "digital_number" );
    REQUIRE( writeTestScene( a ) );
    REQUIRE( writeTestScene( b ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "sr.tif" ).toStdString() );
    scenes.append( fx.filePath( "dn.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "x.tif" ) ).toStdString();

    bool threw = false;
    try
    {
        runOp( "rs:temporal_summary", params );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        REQUIRE( QString::fromStdString( e.message() ).contains( QStringLiteral( "radiometric" ) ) );
    }
    REQUIRE( threw );
    REQUIRE( !QFile::exists( fx.filePath( QStringLiteral( "x.tif" ) ) ) );
}

TEST_CASE( "temporal operators: grid mismatch rejected before compute (§46)",
           "[temporal][operators][preflight]" )
{
    ensureApp();
    Fixture fx;
    TestScene a = makeTestScene( fx.filePath( "g1.tif" ), QStringLiteral( "2025-01-01" ), { 1.0f }, 1, 1 );
    TestScene b = makeTestScene( fx.filePath( "g2.tif" ), QStringLiteral( "2025-01-02" ), { 2.0f }, 1, 1 );
    b.gt[0] = 500010.0; // same pixel size, shifted origin
    REQUIRE( writeTestScene( a ) );
    REQUIRE( writeTestScene( b ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "g1.tif" ).toStdString() );
    scenes.append( fx.filePath( "g2.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "g.tif" ) ).toStdString();
    bool threw = false;
    try
    {
        runOp( "rs:temporal_trend", params );
    }
    catch ( const RSOperatorError & )
    {
        threw = true;
    }
    REQUIRE( threw );
}

TEST_CASE( "temporal operators: determinism — identical reruns byte-stable (§50)",
           "[temporal][operators][determinism]" )
{
    ensureApp();
    Fixture fx;
    QDir d( fx.dir.path() );
    d.mkdir( QStringLiteral( "det" ) );
    Json::Value scenes( Json::arrayValue );
    for ( int i = 0; i < 3; ++i )
    {
        const QString p = fx.filePath( QStringLiteral( "det/s%1.tif" ).arg( i ) );
        REQUIRE( writeTestScene(
            makeTestScene( p, QStringLiteral( "2025-03-%1" ).arg( i + 1, 2, 10, QChar( '0' ) ),
                           { 1.0f * i, 4.0f - i, 2.0f + i, 7.0f }, 2, 2 ) ) );
        scenes.append( p.toStdString() );
    }
    auto runTo = [&]( const QString &out ) {
        Json::Value params( Json::objectValue );
        params["scenes"] = scenes;
        params["band"] = 1;
        params["output"] = out.toStdString();
        runOp( "rs:temporal_summary", params );
        return readBand( out, 3 );
    };
    const auto first = runTo( fx.filePath( QStringLiteral( "det1.tif" ) ) );
    const auto second = runTo( fx.filePath( QStringLiteral( "det2.tif" ) ) );
    REQUIRE( first == second );
}
