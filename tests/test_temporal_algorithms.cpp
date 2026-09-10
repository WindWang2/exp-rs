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

// ------------------------------------------------------------ gap fill ----

TEST_CASE( "temporal_gap_fill: linear interpolation over real day offsets E2E",
           "[temporal][operators][gapfill]" )
{
    ensureApp();
    Fixture fx;

    // 4 dates 10 days apart, 2x1 pixels (raster is width-major):
    // pixel 0: 10, gap, gap, 40  → linear fills 20 (t=11), 30 (t=21)
    // pixel 1: gap, 20, gap, 80  → leading gap stays NaN (linear never
    //          extrapolates); interior gap fills 20 + (80−20)/2 = 50.
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "a.tif" ), QStringLiteral( "2025-01-01" ),
                                            { 10, -9999 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b.tif" ), QStringLiteral( "2025-01-11" ),
                                            { -9999, 20 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "c.tif" ), QStringLiteral( "2025-01-21" ),
                                            { -9999, -9999 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "d.tif" ), QStringLiteral( "2025-01-31" ),
                                            { 40, 80 }, 2, 1 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "a.tif", "b.tif", "c.tif", "d.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["method"] = "linear";
    params["output"] = fx.filePath( QStringLiteral( "filled.tif" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_gap_fill", params );
    REQUIRE( result["sceneCount"].asInt() == 4 );
    // filled/fillable: pixel 0 fills 2 of 2 anchored; pixel 1 fills 1 of 2
    // (the leading gap has no left anchor — fillable, never extrapolated).
    REQUIRE( result["filledFraction"].asDouble() == Approx( 3.0 / 4.0 ) );

    REQUIRE( readBand( fx.filePath( "filled.tif" ), 1 )[0] == Approx( 10.0 ) );
    REQUIRE( readBand( fx.filePath( "filled.tif" ), 2 )[0] == Approx( 20.0 ) );
    REQUIRE( readBand( fx.filePath( "filled.tif" ), 3 )[0] == Approx( 30.0 ) );
    REQUIRE( readBand( fx.filePath( "filled.tif" ), 4 )[0] == Approx( 40.0 ) );

    const auto p1b1 = readBand( fx.filePath( "filled.tif" ), 1 );
    const auto p1b2 = readBand( fx.filePath( "filled.tif" ), 2 );
    const auto p1b4 = readBand( fx.filePath( "filled.tif" ), 4 );
    REQUIRE( std::isnan( p1b1[1] ) );                       // leading gap
    REQUIRE( p1b2[1] == Approx( 20.0 ) );                   // valid copy-through
    REQUIRE( p1b4[1] == Approx( 80.0 ) );
    const auto p1b3 = readBand( fx.filePath( "filled.tif" ), 3 );
    REQUIRE( p1b3[1] == Approx( 50.0 ) );                   // 20 + 60·(10/20)

    // filled_count band: pixel 0 filled 2; pixel 1 filled 1.
    const auto countBand = readBand( fx.filePath( "filled.tif" ), 5 );
    REQUIRE( countBand[0] == Approx( 2 ) );
    REQUIRE( countBand[1] == Approx( 1 ) );
}

TEST_CASE( "temporal_gap_fill: nearest fills one-sided gaps, ties take the earlier scene",
           "[temporal][operators][gapfill]" )
{
    ensureApp();
    Fixture fx;

    // Same 4-date collection; nearest semantics:
    // pixel 0: 10, gap(t=11), gap(t=21), 40 → t=11: left 10 days vs right 20
    // → 10; t=21: right 10 days vs left 20 → 40.
    // pixel 1: gap(t=1), 20(t=11), gap(t=21), 80(t=31) → leading gap fills
    // from t=11 (10 days, nearest DOES use one-sided anchors): 20; interior
    // tie 10/10 → earlier scene → 20.
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "a.tif" ), QStringLiteral( "2025-01-01" ),
                                            { 10, -9999 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b.tif" ), QStringLiteral( "2025-01-11" ),
                                            { -9999, 20 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "c.tif" ), QStringLiteral( "2025-01-21" ),
                                            { -9999, -9999 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "d.tif" ), QStringLiteral( "2025-01-31" ),
                                            { 40, 80 }, 2, 1 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "a.tif", "b.tif", "c.tif", "d.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["method"] = "nearest";
    params["output"] = fx.filePath( QStringLiteral( "filled.tif" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_gap_fill", params );
    REQUIRE( result["filledFraction"].asDouble() == Approx( 1.0 ) );

    REQUIRE( readBand( fx.filePath( "filled.tif" ), 2 )[0] == Approx( 10.0 ) );
    REQUIRE( readBand( fx.filePath( "filled.tif" ), 3 )[0] == Approx( 40.0 ) );
    REQUIRE( readBand( fx.filePath( "filled.tif" ), 1 )[1] == Approx( 20.0 ) );
    REQUIRE( readBand( fx.filePath( "filled.tif" ), 3 )[1] == Approx( 20.0 ) );
    REQUIRE( readBand( fx.filePath( "filled.tif" ), 4 )[1] == Approx( 80.0 ) );
    // filled_count: every gap position filled (pixel 0: 2, pixel 1: 2).
    const auto countBand = readBand( fx.filePath( "filled.tif" ), 5 );
    REQUIRE( countBand[0] == Approx( 2 ) );
    REQUIRE( countBand[1] == Approx( 2 ) );
}

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
    // writeBand reads width*height floats: declare a 2x1 raster or the 2
    // floats per scene overflow the 2x2 default (ASan heap-buffer-overflow).
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "a.tif" ), QStringLiteral( "2025-01-01" ),
                                            { 10, -9999 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b.tif" ), QStringLiteral( "2025-01-02" ),
                                            { 20, 20 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "c.tif" ), QStringLiteral( "2025-01-03" ),
                                            { -9999, 30 }, 2, 1 ) ) );
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
    // One pixel per scene: declare 1x1 or writeBand overflows the buffer.
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "m1.tif" ), QStringLiteral( "2025-01-15" ), { 1.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "m2.tif" ), QStringLiteral( "2025-02-10" ), { 2.0f }, 1, 1 ) ) );
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

TEST_CASE( "temporal_sen_trend: hand-derived Sen slope and Mann-Kendall p-value",
           "[temporal][operators][sen]" )
{
    ensureApp();
    Fixture fx;
    // y = [1, 2, 6] at days 0, 2, 10: all three pairwise day slopes are 0.5,
    // so Sen's slope = 0.5 and the residual set y − 0.5·t = {1, 1, 1} gives
    // intercept 1. S = 3 (strictly increasing); var(S) = 3·2·11/18 = 66/18
    // (no ties); z = (S−1)/sqrt(var) with the continuity correction.
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "s0.tif" ), QStringLiteral( "2025-01-01" ), { 1.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "s2.tif" ), QStringLiteral( "2025-01-03" ), { 2.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "s10.tif" ), QStringLiteral( "2025-01-11" ), { 6.0f }, 1, 1 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "s0.tif" ).toStdString() );
    scenes.append( fx.filePath( "s2.tif" ).toStdString() );
    scenes.append( fx.filePath( "s10.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "sen.tif" ) ).toStdString();
    const Json::Value result = runOp( "rs:temporal_sen_trend", params );
    REQUIRE( result["bands"].asInt() == 5 );

    const auto slope = readBand( fx.filePath( QStringLiteral( "sen.tif" ) ), 1 );
    const auto intercept = readBand( fx.filePath( QStringLiteral( "sen.tif" ) ), 2 );
    const auto z = readBand( fx.filePath( QStringLiteral( "sen.tif" ) ), 3 );
    const auto p = readBand( fx.filePath( QStringLiteral( "sen.tif" ) ), 4 );
    const auto n = readBand( fx.filePath( QStringLiteral( "sen.tif" ) ), 5 );
    REQUIRE( slope[0] == Approx( 0.5 ) );
    REQUIRE( intercept[0] == Approx( 1.0 ) );
    const double expectedVar = 66.0 / 18.0;
    REQUIRE( z[0] == Approx( ( 3.0 - 1.0 ) / std::sqrt( expectedVar ) ).epsilon( 1e-6 ) );
    REQUIRE( p[0] == Approx( std::erfc( ( ( 3.0 - 1.0 ) / std::sqrt( expectedVar ) ) / std::sqrt( 2.0 ) ) )
                 .epsilon( 1e-6 ) );
    REQUIRE( n[0] == Approx( 3 ) );
}

TEST_CASE( "temporal_sen_trend: too few valid observations yield NaN, not zero",
           "[temporal][operators][sen]" )
{
    ensureApp();
    Fixture fx;
    // valid = {0 at day 0, 1 at day 4}: validCount 2 < 3 → all metrics NaN.
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "q0.tif" ), QStringLiteral( "2025-01-01" ), { 0.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "q2.tif" ), QStringLiteral( "2025-01-03" ), { -9999.0f }, 1, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "q4.tif" ), QStringLiteral( "2025-01-05" ), { 1.0f }, 1, 1 ) ) );
    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "q0.tif", "q2.tif", "q4.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "sen2.tif" ) ).toStdString();
    runOp( "rs:temporal_sen_trend", params );
    const auto slope = readBand( fx.filePath( QStringLiteral( "sen2.tif" ) ), 1 );
    const auto p = readBand( fx.filePath( QStringLiteral( "sen2.tif" ) ), 4 );
    const auto n = readBand( fx.filePath( QStringLiteral( "sen2.tif" ) ), 5 );
    REQUIRE( std::isnan( slope[0] ) );
    REQUIRE( std::isnan( p[0] ) );
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

// ------------------------------------------------ temporal_monitor (F5.0 E) —

TEST_CASE( "temporal_monitor cusum: hand-derived standardized cumulative sum",
           "[temporal][operators][monitor]" )
{
    ensureApp();
    Fixture fx;
    // 2x1 grid, 4 dates. pixel 0: 0,0,0,10 -> mu=2.5, sample sd=5,
    // z = (-0.5,-0.5,-0.5,1.5) -> S = -0.5,-1,-1.5,0 (final 0, max|S| 1.5 @ 2).
    // pixel 1: constant 5 -> sd 0 -> z = 0 -> S = 0, argmax undefined (NaN).
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "a.tif" ), QStringLiteral( "2025-01-01" ), { 0, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b.tif" ), QStringLiteral( "2025-01-11" ), { 0, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "c.tif" ), QStringLiteral( "2025-01-21" ), { 0, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "d.tif" ), QStringLiteral( "2025-01-31" ), { 10, 5 }, 2, 1 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "a.tif", "b.tif", "c.tif", "d.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["method"] = "cusum";
    params["output"] = fx.filePath( QStringLiteral( "monitor.tif" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_monitor", params );
    REQUIRE( result["sceneCount"].asInt() == 4 );

    const auto s = readBand( fx.filePath( QStringLiteral( "monitor.tif" ) ), 1 );
    const auto mx = readBand( fx.filePath( QStringLiteral( "monitor.tif" ) ), 2 );
    const auto arg = readBand( fx.filePath( QStringLiteral( "monitor.tif" ) ), 3 );
    REQUIRE( s[0] == Approx( 0.0 ).margin( 1e-9 ) );
    REQUIRE( mx[0] == Approx( 1.5 ).margin( 1e-9 ) );
    REQUIRE( arg[0] == Approx( 2.0 ) );
    REQUIRE( s[1] == Approx( 0.0 ).margin( 1e-9 ) );
    REQUIRE( mx[1] == Approx( 0.0 ).margin( 1e-9 ) );
    REQUIRE( std::isnan( arg[1] ) );
}

TEST_CASE( "temporal_monitor ewma: hand-derived recursion", "[temporal][operators][monitor]" )
{
    ensureApp();
    Fixture fx;
    // Same series as cusum; lambda = 0.5:
    // Z = -0.25, -0.125, -0.0625, 0.5*(-0.0625)+0.5*1.5 = 0.71875 (final+max @ 3).
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "a.tif" ), QStringLiteral( "2025-01-01" ), { 0, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "b.tif" ), QStringLiteral( "2025-01-11" ), { 0, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "c.tif" ), QStringLiteral( "2025-01-21" ), { 0, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "d.tif" ), QStringLiteral( "2025-01-31" ), { 10, 5 }, 2, 1 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "a.tif", "b.tif", "c.tif", "d.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["method"] = "ewma";
    params["lambda"] = 0.5;
    params["output"] = fx.filePath( QStringLiteral( "ewma.tif" ) ).toStdString();

    REQUIRE( runOp( "rs:temporal_monitor", params )["sceneCount"].asInt() == 4 );
    const auto z = readBand( fx.filePath( QStringLiteral( "ewma.tif" ) ), 1 );
    const auto mx = readBand( fx.filePath( QStringLiteral( "ewma.tif" ) ), 2 );
    const auto arg = readBand( fx.filePath( QStringLiteral( "ewma.tif" ) ), 3 );
    REQUIRE( z[0] == Approx( 0.53125 ).margin( 1e-9 ) );
    REQUIRE( mx[0] == Approx( 0.53125 ).margin( 1e-9 ) );
    REQUIRE( arg[0] == Approx( 3.0 ) );
}

TEST_CASE( "temporal_monitor seasonal_mk: hand-derived two-season statistic",
           "[temporal][operators][monitor]" )
{
    ensureApp();
    Fixture fx;
    // Pixel 0: Jan {1,2} (S=+1), Feb {4,3,2,1} (S=-6) -> S=-5, pairs=1+6=7,
    // var = 1 + 156/18 = 9.667, Z = (-5+1)/sqrt(9.667) = -1.2863,
    // tau = -5/7 = -0.7143, seasonsUsed = 2.
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "j1.tif" ), QStringLiteral( "2025-01-01" ), { 1, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "j2.tif" ), QStringLiteral( "2025-01-15" ), { 2, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "f1.tif" ), QStringLiteral( "2025-02-01" ), { 4, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "f2.tif" ), QStringLiteral( "2025-02-10" ), { 3, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "f3.tif" ), QStringLiteral( "2025-02-20" ), { 2, 5 }, 2, 1 ) ) );
    REQUIRE( writeTestScene( makeTestScene( fx.filePath( "f4.tif" ), QStringLiteral( "2025-02-28" ), { 1, 5 }, 2, 1 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "j1.tif", "j2.tif", "f1.tif", "f2.tif", "f3.tif", "f4.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["method"] = "seasonal_mk";
    params["min_observations"] = 3;
    params["output"] = fx.filePath( QStringLiteral( "smk.tif" ) ).toStdString();

    REQUIRE( runOp( "rs:temporal_monitor", params )["sceneCount"].asInt() == 6 );
    const auto z = readBand( fx.filePath( QStringLiteral( "smk.tif" ) ), 1 );
    const auto tau = readBand( fx.filePath( QStringLiteral( "smk.tif" ) ), 2 );
    const auto seasons = readBand( fx.filePath( QStringLiteral( "smk.tif" ) ), 3 );
    REQUIRE( z[0] == Approx( -4.0 / std::sqrt( 1.0 + 156.0 / 18.0 ) ).margin( 1e-9 ) );
    REQUIRE( tau[0] == Approx( -5.0 / 7.0 ).margin( 1e-9 ) );
    REQUIRE( seasons[0] == Approx( 2.0 ) );
}

TEST_CASE( "temporal_monitor: ewma lambda and seasonal_mk pairwork are typed refusals",
           "[temporal][operators][monitor][contract]" )
{
    ensureApp();
    Fixture fx;
    const char *names[4] = { "a.tif", "b.tif", "c.tif", "d.tif" };
    const char *dates[4] = { "2025-01-01", "2025-02-01", "2025-03-01", "2025-04-01" };
    for ( int i = 0; i < 4; ++i )
        REQUIRE( writeTestScene( makeTestScene( fx.filePath( names[i] ), QString::fromUtf8( dates[i] ),
                                                { float( i + 1 ), float( i + 1 ) }, 2, 1 ) ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : names )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "m.tif" ) ).toStdString();

    params["method"] = "ewma";
    params["lambda"] = 1.5;
    REQUIRE_THROWS_AS( runOp( "rs:temporal_monitor", params ), RSOperatorError );

    // Pairwork = scenes^2/2 x tilePixels = 16/2 x 4 = 32 > max_pairwork 1.
    params["method"] = "seasonal_mk";
    params["max_pairwork"] = 1;
    REQUIRE_THROWS_AS( runOp( "rs:temporal_monitor", params ), RSOperatorError );
}

// ---------------------------------------------------------------------------
// Operator E2E for the smooth / harmonic_fit / phenology / breakpoints
// families (Temporal 7.0): the kernels behind them are known-answer tested
// in test_temporal_fit.cpp; these pin the full streaming path — collection
// input, per-tile series, band layout, and output metadata.
// ---------------------------------------------------------------------------

/// 1-based GDAL band index whose description contains @a needle.
int findBandByDescription( const QString &path, const QString &needle )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return -1;
    const int count = ds.bandCount();
    for ( int b = 1; b <= count; ++b )
    {
        const char *desc =
            GDALGetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( ds.dataset() ), b ) );
        if ( desc != nullptr && QString::fromUtf8( desc ).contains( needle ) )
            return b;
    }
    return -1;
}

TEST_CASE( "temporal_smooth: Savitzky-Golay preserves linear ramps E2E",
           "[temporal][operators][smooth]" )
{
    ensureApp();
    Fixture fx;

    // 6 dates, 10 days apart, 2x1 pixels: pixel 0 is a linear ramp, pixel 1
    // is constant. SG of degree >= 1 over ANY odd window reproduces
    // polynomials exactly (up to float noise) — a param-independent
    // known answer for the whole streaming path.
    const float ramp[] = { 10, 20, 30, 40, 50, 60 };
    for ( int i = 0; i < 6; ++i )
    {
        const QString date = QDate( 2025, 1, 1 ).addDays( 10 * i ).toString( Qt::ISODate );
        const float v = ramp[i];
        REQUIRE( writeTestScene( makeTestScene( fx.filePath( QStringLiteral( "s%1.tif" ).arg( i ) ),
                                                date, { v, 5.0f }, 2, 1 ) ) );
    }

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( int i = 0; i < 6; ++i )
        scenes.append( fx.filePath( QStringLiteral( "s%1.tif" ).arg( i ) ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["method"] = "savitzky_golay";
    params["window"] = 5;
    params["degree"] = 2;
    params["output"] = fx.filePath( QStringLiteral( "smooth.tif" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_smooth", params );
    REQUIRE( result["bands"].asInt() == 6 );

    for ( int s = 0; s < 6; ++s )
    {
        const auto out = readBand( fx.filePath( "smooth.tif" ), s + 1 );
        REQUIRE( out[0] == Approx( ramp[s] ).margin( 1e-3 ) );
        REQUIRE( out[1] == Approx( 5.0 ).margin( 1e-3 ) );
    }
}

TEST_CASE( "temporal_harmonic_fit: recovers the sine coefficients E2E",
           "[temporal][operators][harmonic]" )
{
    ensureApp();
    Fixture fx;

    // y(doy) = 0.2 + 0.1·sin(2π·doy/365.25) sampled every 10 days over a
    // year: one harmonic is the exact model, so the fit must recover
    // intercept 0.2, sin coefficient 0.1, near-zero RMSE.
    const int nScenes = 36;
    for ( int i = 0; i < nScenes; ++i )
    {
        const int doy = 1 + 10 * i;
        const QString date = QDate( 2025, 1, 1 ).addDays( doy - 1 ).toString( Qt::ISODate );
        const float v = static_cast<float>(
            0.2 + 0.1 * std::sin( 2.0 * M_PI * doy / 365.25 ) );
        REQUIRE( writeTestScene(
            makeTestScene( fx.filePath( QStringLiteral( "h%1.tif" ).arg( i ) ),
                           date, { v, v }, 2, 1 ) ) );
    }

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( int i = 0; i < nScenes; ++i )
        scenes.append( fx.filePath( QStringLiteral( "h%1.tif" ).arg( i ) ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["harmonics"] = 1;
    params["writeCoefficients"] = true;
    params["output"] = fx.filePath( QStringLiteral( "fit.tif" ) ).toStdString();

    REQUIRE_NOTHROW( runOp( "rs:temporal_harmonic_fit", params ) );

    // Documented band layout after the fitted_1..N bands: rmse, r2,
    // coef_intercept, then two coefficient bands per harmonic (sin, cos).
    const int rmseBand = nScenes + 1;
    const int interceptBand = nScenes + 3;
    const int sinBand = nScenes + 4;

    const auto rmse = readBand( fx.filePath( "fit.tif" ), rmseBand );
    const auto intercept = readBand( fx.filePath( "fit.tif" ), interceptBand );
    const auto sinCoef = readBand( fx.filePath( "fit.tif" ), sinBand );
    REQUIRE( rmse[0] == Approx( 0.0 ).margin( 0.01 ) );
    REQUIRE( intercept[0] == Approx( 0.2 ).margin( 0.01 ) );
    REQUIRE( sinCoef[0] == Approx( 0.1 ).margin( 0.01 ) );

    // The fitted series reproduces the input at every sample date.
    for ( int i = 0; i < nScenes; ++i )
    {
        const int doy = 1 + 10 * i;
        const float v = static_cast<float>(
            0.2 + 0.1 * std::sin( 2.0 * M_PI * doy / 365.25 ) );
        const auto fitted = readBand( fx.filePath( "fit.tif" ), i + 1 );
        REQUIRE( fitted[0] == Approx( v ).margin( 0.01 ) );
    }
}

TEST_CASE( "temporal_phenology: analytic SOS/POS/EOS on a sine season E2E",
           "[temporal][operators][phenology]" )
{
    ensureApp();
    Fixture fx;

    // y(doy) = 0.2 + 0.3·sin(2π(doy − 91.3)/365.25), sampled every 5 days.
    // The threshold crossings are fractions of the observed (max−min)
    // range: min = −0.1, max = 0.5, crossing level = −0.1 + 0.2·0.6 = 0.02,
    // i.e. sin(θ) = −0.6: SOS at θ = asin(−0.6) → doy ≈ 53.9, POS at the
    // crest ≈ 182.6, EOS at θ = π − asin(−0.6) → doy ≈ 311.1. Five-day
    // sampling discretizes the crossings by ±2 days; margins absorb that
    // plus the harmonic fit's share.
    const int nScenes = 73;
    for ( int i = 0; i < nScenes; ++i )
    {
        const int doy = 1 + 5 * i;
        const QString date = QDate( 2025, 1, 1 ).addDays( doy - 1 ).toString( Qt::ISODate );
        const float v = static_cast<float>(
            0.2 + 0.3 * std::sin( 2.0 * M_PI * ( doy - 91.3 ) / 365.25 ) );
        REQUIRE( writeTestScene(
            makeTestScene( fx.filePath( QStringLiteral( "p%1.tif" ).arg( i ) ),
                           date, { v, v }, 2, 1 ) ) );
    }

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( int i = 0; i < nScenes; ++i )
        scenes.append( fx.filePath( QStringLiteral( "p%1.tif" ).arg( i ) ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "phen.tif" ) ).toStdString();

    REQUIRE_NOTHROW( runOp( "rs:temporal_phenology", params ) );

    const auto sos = readBand( fx.filePath( "phen.tif" ), 1 );
    const auto pos = readBand( fx.filePath( "phen.tif" ), 2 );
    const auto eos = readBand( fx.filePath( "phen.tif" ), 3 );
    REQUIRE( sos[0] == Approx( 53.9 ).margin( 8.0 ) );
    REQUIRE( pos[0] == Approx( 182.6 ).margin( 5.0 ) );
    REQUIRE( eos[0] == Approx( 311.1 ).margin( 12.0 ) );
    // Season length is consistent with its endpoints.
    REQUIRE( ( eos[0] - sos[0] ) == Approx( 257.2 ).margin( 15.0 ) );
}

TEST_CASE( "temporal_breakpoints: locates a step change E2E",
           "[temporal][operators][breakpoints]" )
{
    ensureApp();
    Fixture fx;

    // 12 dates, 10 days apart: 10,10,...,10 then 30,30,...,30. The true
    // break lies between doy 51 and 61 (midpoint 56); the piecewise-linear
    // fit must place break_date_1 within a scene or two.
    for ( int i = 0; i < 12; ++i )
    {
        const QString date = QDate( 2025, 1, 1 ).addDays( 10 * i ).toString( Qt::ISODate );
        const float v = i < 6 ? 10.0f : 30.0f;
        REQUIRE( writeTestScene( makeTestScene( fx.filePath( QStringLiteral( "b%1.tif" ).arg( i ) ),
                                                date, { v, v }, 2, 1 ) ) );
    }

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( int i = 0; i < 12; ++i )
        scenes.append( fx.filePath( QStringLiteral( "b%1.tif" ).arg( i ) ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "brk.tif" ) ).toStdString();

    REQUIRE_NOTHROW( runOp( "rs:temporal_breakpoints", params ) );

    const int breakBand = findBandByDescription( fx.filePath( "brk.tif" ), "break_date_1" );
    REQUIRE( breakBand > 0 );
    const auto breakDay = readBand( fx.filePath( "brk.tif" ), breakBand );
    REQUIRE( breakDay[0] == Approx( 56.0 ).margin( 15.0 ) );
}
