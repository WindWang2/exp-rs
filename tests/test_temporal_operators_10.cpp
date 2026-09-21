// tests/test_temporal_operators_10.cpp — operator end-to-end tests for the
// Temporal Platform 10.0 increments: rs:temporal_regularize (T-2),
// rs:temporal_harmonic_breaks (T-3), rs:temporal_extract_regions (C-2),
// rs:temporal_region_features (G), the monitor scenes seam (T-1) and the
// phenology dual-cycle extension. Synthetic GeoTIFF fixtures only.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>

#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
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
char appArgv0[] = "test_temporal_operators_10";
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
    int height = 1;
    std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
    QString date;
    bool declareNodata = true;
    double nodata = -9999.0;
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

    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( driver, s.path.toUtf8().constData(), s.width, s.height,
                                  1, GDT_Float32, nullptr );
    if ( !ds )
        return false;
    GDALSetGeoTransform( ds, const_cast<double *>( s.gt.data() ) );
    GDALSetProjection( ds, wkt.toUtf8().constData() );
    if ( !s.date.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_ACQUISITION_DATE", s.date.toUtf8().constData(),
                             nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    if ( s.declareNodata )
        GDALSetRasterNoDataValue( band, s.nodata );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, s.width, s.height,
                                  const_cast<float *>( s.values.data() ), s.width, s.height,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

Json::Value runOp( const char *name, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( name );
    REQUIRE( op != nullptr );
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

std::vector<float> readBand( const QString &path, int band = 1 )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    REQUIRE( ds.readBandData( band, out.data(), ds.width(), ds.height() ) );
    return out;
}

int bandCount( const QString &path )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    return ds.bandCount();
}

struct Fixture
{
    QTemporaryDir dir;
    Fixture() { REQUIRE( dir.isValid() ); }
    QString filePath( const QString &name ) const { return dir.filePath( name ); }
};

/// A weekly seasonal series with an optional regime change at sample
/// @a breakSample (level drop @a drop, slope flip to @a slopeAfter).
struct SeriesSpec
{
    double seasonalAmp = 2.0;
    double slopeBefore = 0.005;
    double slopeAfter = 0.005;
    double drop = 0.0;
    int breakSample = -1;
    double base = 20.0;
    int stepDays = 7;
    int count = 120;
};

std::pair<std::vector<float>, std::vector<double>> seriesAndDays( const SeriesSpec &spec )
{
    std::vector<float> y;
    std::vector<double> t;
    y.reserve( static_cast<size_t>( spec.count ) );
    t.reserve( static_cast<size_t>( spec.count ) );
    for ( int i = 0; i < spec.count; ++i )
    {
        const double ti = spec.stepDays * i;
        t.push_back( ti );
        const double seasonal =
            spec.seasonalAmp * std::sin( 2.0 * M_PI * ti / 365.25 );
        const bool after = spec.breakSample >= 0 && i >= spec.breakSample;
        // Trend continuous at the break; slope flips to slopeAfter after it.
        const double tRef = spec.stepDays * std::max( spec.breakSample, 0 );
        const double trendValue =
            after ? spec.slopeBefore * tRef + spec.slopeAfter * ( ti - tRef )
                  : spec.slopeBefore * ti;
        const double levelDrop = after ? spec.drop : 0.0;
        y.push_back( static_cast<float>( spec.base + trendValue + seasonal + levelDrop ) );
    }
    return { std::move( y ), std::move( t ) };
}

} // namespace

// ---------------------------------------------------------- regularize ----

TEST_CASE( "temporal_regularize: 10-day calendar with linear interpolation E2E",
           "[temporal][operators][regularize][t2]" )
{
    ensureApp();
    Fixture fx;

    // Observations at t = 0, 10, 20, 30; pixel 0 linear ramp 10..40, pixel 1
    // has a gap at t = 10 (stays a filled interpolation 20→50·? — see below).
    REQUIRE( writeTestScene( { fx.filePath( "a.tif" ), { 10, 20 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-01-01" } ) );
    REQUIRE( writeTestScene( { fx.filePath( "b.tif" ), { 20, -9999 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-01-11" } ) );
    REQUIRE( writeTestScene( { fx.filePath( "c.tif" ), { 30, 40 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-01-21" } ) );
    REQUIRE( writeTestScene( { fx.filePath( "d.tif" ), { 40, 60 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-01-31" } ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "a.tif", "b.tif", "c.tif", "d.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["cadence"] = "10d";
    params["method"] = "linear";
    params["output"] = fx.filePath( QStringLiteral( "reg.tif" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_regularize", params );
    REQUIRE( result["calendarPoints"].asInt() == 4 );
    REQUIRE( result["calendarStart"].asString() == "2025-01-01" );
    REQUIRE( result["calendarEnd"].asString() == "2025-01-31" );
    REQUIRE( result["bands"].asInt() == 6 ); // 4 calendar + valid_count + filled_count

    // Pixel 0: observations on every calendar point, all copy through.
    const auto b1 = readBand( fx.filePath( "reg.tif" ), 1 );
    REQUIRE( b1[0] == Approx( 10.0 ) );
    // Pixel 1: t=10 interpolated between 20 (t=0) and 40 (t=20) → 30.
    const auto b2 = readBand( fx.filePath( "reg.tif" ), 2 );
    REQUIRE( b2[1] == Approx( 30.0 ) );

    const auto validCount = readBand( fx.filePath( "reg.tif" ), 5 );
    const auto filledCount = readBand( fx.filePath( "reg.tif" ), 6 );
    // valid_count sums per-point contributions over the calendar: pixel 0
    // sits on an observation at every point (4 × 1), pixel 1 interpolates
    // at t=10 (2 anchors) and sits on observations elsewhere.
    REQUIRE( validCount[0] == Approx( 4 ) );
    REQUIRE( filledCount[0] == Approx( 0 ) );
    REQUIRE( validCount[1] == Approx( 5 ) ); // 1+2+1+1
    REQUIRE( filledCount[1] == Approx( 1 ) ); // t=10 pixel 1 is synthetic
}

TEST_CASE( "temporal_regularize: refuses a cadence that explodes the calendar",
           "[temporal][operators][regularize]" )
{
    ensureApp();
    Fixture fx;
    REQUIRE( writeTestScene( { fx.filePath( "a.tif" ), { 1 }, 1, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2000-01-01" } ) );
    REQUIRE( writeTestScene( { fx.filePath( "b.tif" ), { 2 }, 1, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2050-01-01" } ) );
    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    scenes.append( fx.filePath( "a.tif" ).toStdString() );
    scenes.append( fx.filePath( "b.tif" ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["cadence"] = "1d";
    params["output"] = fx.filePath( QStringLiteral( "reg.tif" ) ).toStdString();
    bool threw = false;
    try
    {
        runOp( "rs:temporal_regularize", params );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        REQUIRE( std::string( e.what() ).find( "calendar" ) != std::string::npos );
    }
    REQUIRE( threw );
}

// ------------------------------------------------------ harmonic breaks ----

TEST_CASE( "temporal_harmonic_breaks: localizes a deforestation-like regime change",
           "[temporal][operators][harmonic-breaks][t3]" )
{
    ensureApp();
    Fixture fx;

    // Weekly 2.5-year series with a level drop + slope flip at sample 90
    // (day 630). Pixel 0 changes, pixel 1 stays clean.
    SeriesSpec changed;
    changed.breakSample = 90;
    changed.drop = -6.0;
    changed.slopeAfter = -0.01;
    SeriesSpec clean;
    const auto [y0, tDays] = seriesAndDays( changed );
    const auto [y1, tDays2] = seriesAndDays( clean );
    Q_UNUSED( tDays2 );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    QStringList dates;
    for ( int i = 0; i < y0.size(); ++i )
    {
        const QString name = QStringLiteral( "s%1.tif" ).arg( i, 3, 10, QLatin1Char( '0' ) );
        TestScene s;
        s.path = fx.filePath( name );
        s.date = QDate( 2024, 1, 1 ).addDays( 7 * i ).toString( Qt::ISODate );
        s.values = { y0[i], y1[i] };
        REQUIRE( writeTestScene( s ) );
        scenes.append( s.path.toStdString() );
        dates << s.date;
    }
    params["scenes"] = scenes;
    params["band"] = 1;
    params["harmonics"] = 2;
    params["maxBreaks"] = 2;
    params["minSegmentDays"] = 90.0;
    params["minImprovement"] = 0.1;
    params["direction"] = "decrease";
    params["output"] = fx.filePath( QStringLiteral( "breaks.tif" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_harmonic_breaks", params );
    REQUIRE( result["sceneCount"].asInt() == y0.size() );
    REQUIRE( result["epochDate"].asString() == "2024-01-01" );

    // Band layout: count + 2×day + 2×mag + slope_first/slope_last + rmse + r2
    // + valid + onset + recovery = 12.
    REQUIRE( bandCount( fx.filePath( "breaks.tif" ) ) == 12 );

    const auto count = readBand( fx.filePath( "breaks.tif" ), 1 );
    const auto day1 = readBand( fx.filePath( "breaks.tif" ), 2 );
    const auto mag1 = readBand( fx.filePath( "breaks.tif" ), 4 );
    const auto slopeFirst = readBand( fx.filePath( "breaks.tif" ), 6 );
    const auto slopeLast = readBand( fx.filePath( "breaks.tif" ), 7 );
    const auto onsetDay = readBand( fx.filePath( "breaks.tif" ), 11 );
    const auto recovery = readBand( fx.filePath( "breaks.tif" ), 12 );

    REQUIRE( count[0] >= 1 );
    REQUIRE( std::abs( day1[0] - 630.0 ) <= 35.0 ); // within 5 samples
    REQUIRE( mag1[0] >= 3.0 );
    REQUIRE( slopeFirst[0] > 0.0 );
    REQUIRE( slopeLast[0] < 0.0 );
    REQUIRE( onsetDay[0] == Approx( day1[0] ).margin( 1.0 ) );

    // The clean pixel may show weak breaks from noise-free series it should
    // have none: the greedy gate on a pure sinusoid+trend accepts no split.
    REQUIRE( count[1] == 0 );
    REQUIRE( std::isnan( day1[1] ) );
    REQUIRE( std::isnan( onsetDay[1] ) );
}

// ------------------------------------------------------- monitor scenes ----

TEST_CASE( "temporal_monitor accepts a scenes array (T-1 regression)",
           "[temporal][operators][monitor][t1]" )
{
    ensureApp();
    // Schema-level pin: the T-1 fix declares 'scenes' and relaxes 'required'
    // to {output, method} — a revert of the schema change fails here even
    // though the run path always accepted scenes.
    {
        auto op = RSOperatorRegistry::instance().create( "rs:temporal_monitor" );
        REQUIRE( op != nullptr );
        const Json::Value schema = op->schema();
        REQUIRE( schema["properties"].isMember( "scenes" ) );
        const Json::Value required = schema["required"];
        REQUIRE( required.size() == 2 );
        bool hasOutput = false;
        bool hasMethod = false;
        for ( const Json::Value &r : required )
        {
            hasOutput = hasOutput || r.asString() == "output";
            hasMethod = hasMethod || r.asString() == "method";
        }
        REQUIRE( hasOutput );
        REQUIRE( hasMethod );
    }
    Fixture fx;
    REQUIRE( writeTestScene( { fx.filePath( "m1.tif" ), { 1, 2 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-01-01" } ) );
    REQUIRE( writeTestScene( { fx.filePath( "m2.tif" ), { 2, 3 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-02-01" } ) );
    REQUIRE( writeTestScene( { fx.filePath( "m3.tif" ), { 9, 4 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-03-01" } ) );
    REQUIRE( writeTestScene( { fx.filePath( "m4.tif" ), { 9, 5 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-04-01" } ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "m1.tif", "m2.tif", "m3.tif", "m4.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["method"] = "cusum";
    params["output"] = fx.filePath( QStringLiteral( "monitor.tif" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_monitor", params );
    REQUIRE( result["sceneCount"].asInt() == 4 );
    REQUIRE( bandCount( fx.filePath( "monitor.tif" ) ) == 3 );
    // The FINAL CUSUM S is ~0 for any series standardized against its own
    // mean; the informative band is max|S_t| — pixel 0 (2 → 9 jump at the
    // end) accumulates a larger excursion than pixel 1 (gentle ramp).
    const auto finalS = readBand( fx.filePath( "monitor.tif" ), 1 );
    const auto maxAbs = readBand( fx.filePath( "monitor.tif" ), 2 );
    REQUIRE( std::abs( finalS[0] ) < 1.0 ); // sums of z cancel
    REQUIRE( maxAbs[0] > maxAbs[1] );
}

TEST_CASE( "temporal_phenology cycles=2: double-cropping bands and cycle_count E2E",
           "[temporal][operators][phenology][t-cycles]" )
{
    ensureApp();
    Fixture fx;

    // Weekly NDVI-like series over two crop cycles: peak around doy 120 and
    // doy 280 each year, troughs between. 2 years, 1 pixel.
    Json::Value scenes( Json::arrayValue );
    const int weeks = 104;
    for ( int i = 0; i < weeks; ++i )
    {
        const int doy = QDate( 2024, 1, 1 ).addDays( 7 * i ).dayOfYear();
        const double bimodal =
            0.5 * std::cos( 2.0 * M_PI * ( doy - 120 ) / 365.25 * 2.0 ) + 0.5;
        const float ndvi = static_cast<float>( 0.15 + 0.7 * bimodal );
        TestScene s;
        s.path = fx.filePath( QStringLiteral( "p%1.tif" ).arg( i, 3, 10, QLatin1Char( '0' ) ) );
        s.date = QDate( 2024, 1, 1 ).addDays( 7 * i ).toString( Qt::ISODate );
        s.width = 1; // single-pixel series
        s.values = { ndvi };
        REQUIRE( writeTestScene( s ) );
        scenes.append( s.path.toStdString() );
    }

    Json::Value params( Json::objectValue );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["cycles"] = 2;
    params["seasonStartDoy"] = 60;
    params["seasonEndDoy"] = 200;
    params["output"] = fx.filePath( QStringLiteral( "ph2.tif" ) ).toStdString();
    const Json::Value result = runOp( "rs:temporal_phenology", params );
    // 11 (cycle 1) + 11 (c2_*) + cycle_count = 23 bands, c2 names pinned.
    // (Temporal Phenology 12.0 added the four limb metrics per cycle.)
    REQUIRE( result["bands"].asInt() == 23 );
    const Json::Value metrics = result["metrics"];
    REQUIRE( metrics.size() == 23 );
    REQUIRE( std::string( metrics[11].asString() ) == "c2_sos" );
    REQUIRE( std::string( metrics[22].asString() ) == "cycle_count" );
    REQUIRE( bandCount( fx.filePath( "ph2.tif" ) ) == 23 );

    // Both cycles are valid on the bimodal pixel; cycle_count = 2.
    const auto c1Sos = readBand( fx.filePath( "ph2.tif" ), 1 );
    CAPTURE( c1Sos[0] );
    REQUIRE( std::isfinite( c1Sos[0] ) ); // cycle 1 defined on [60, 200]
    const auto cycleCount = readBand( fx.filePath( "ph2.tif" ), 23 );
    CAPTURE( cycleCount[0] );
    REQUIRE( cycleCount[0] == Approx( 2 ) );
    // The second cycle's SOS falls in the complement window (doy > 200 or
    // < 60 by the complement of [60, 200] = [201, 59]). Band 12 must hold a
    // real doy (>= 1): an unwritten band reads 0 and would pass the range
    // test vacuously.
    const auto c2Sos = readBand( fx.filePath( "ph2.tif" ), 12 );
    CAPTURE( c2Sos[0] );
    REQUIRE( c2Sos[0] >= 1.0 );
    REQUIRE( ( c2Sos[0] >= 201.0 || c2Sos[0] <= 59.0 ) );
}

// ----------------------------------------------------- extract regions ----

TEST_CASE( "temporal_extract_regions: point + polygon over 3 dates E2E",
           "[temporal][operators][regions][c2]" )
{
    ensureApp();
    Fixture fx;
    REQUIRE( writeTestScene( { fx.filePath( "r1.tif" ), { 10, 20 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-01-01" } ) );
    REQUIRE( writeTestScene( { fx.filePath( "r2.tif" ), { 30, 40 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-01-11" } ) );
    REQUIRE( writeTestScene( { fx.filePath( "r3.tif" ), { 50, 60 }, 2, 1,
                              { 500000, 30, 0, 4500000, 0, -30 }, "2025-01-21" } ) );

    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    for ( const char *p : { "r1.tif", "r2.tif", "r3.tif" } )
        scenes.append( fx.filePath( p ).toStdString() );
    params["scenes"] = scenes;
    params["band"] = 1;
    Json::Value regions( Json::arrayValue );
    Json::Value point( Json::objectValue );
    point["id"] = "pt";
    Json::Value pt( Json::arrayValue );
    pt.append( 500015.0 );  // col 0
    pt.append( 4499985.0 ); // row 0
    point["point"] = pt;
    regions.append( point );
    Json::Value poly( Json::objectValue );
    poly["id"] = "field";
    Json::Value coords( Json::arrayValue );
    const double x0 = 500000.0 + 30.0, x1 = 500000.0 + 60.0;
    const double yTop = 4500000.0, yBot = 4500000.0 - 30.0;
    // 1×1 pixel polygon over col 1, row 0 (the grid is 2×1).
    for ( const auto &[px, py] :
          { std::pair{x0, yTop}, { x1, yTop }, { x1, yBot }, { x0, yBot } } )
    {
        Json::Value c( Json::arrayValue );
        c.append( px );
        c.append( py );
        coords.append( c );
    }
    poly["polygon"] = coords;
    regions.append( poly );
    params["regions"] = regions;
    params["output"] = fx.filePath( QStringLiteral( "regions.csv" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_extract_regions", params );
    REQUIRE( result["regionCount"].asInt() == 2 );
    REQUIRE( result["pointRegions"].asInt() == 1 );
    REQUIRE( result["polygonRegions"].asInt() == 1 );
    REQUIRE( result["rowsWritten"].asInt() == 6 ); // 2 regions × 3 dates
    REQUIRE( result["emptyCells"].asInt() == 0 );
    REQUIRE( result["medianEnabled"].asBool() );

    QFile csv( fx.filePath( "regions.csv" ) );
    REQUIRE( csv.open( QIODevice::ReadOnly | QIODevice::Text ) );
    const QStringList lines = QString::fromUtf8( csv.readAll() ).split( '\n' );
    csv.close();
    REQUIRE( lines.first() ==
             "region_id,date,t_days,mean,min,max,stddev,median,valid_count" );
    int ptRows = 0;
    double fieldMeanAt11 = 0.0;
    for ( const QString &line : lines )
    {
        if ( line.startsWith( "pt," ) )
        {
            ++ptRows;
            const QStringList cols = line.split( ',' );
            REQUIRE( cols.size() == 9 );
            REQUIRE( cols[8].toInt() == 1 ); // one pixel under the point
        }
        if ( line.startsWith( "field,2025-01-11" ) )
        {
            const QStringList cols = line.split( ',' );
            fieldMeanAt11 = cols[3].toDouble();
        }
    }
    REQUIRE( ptRows == 3 );
    REQUIRE( fieldMeanAt11 == Approx( 40.0 ) ); // pixel (1,0) value at date 2
}

// ---------------------------------------------------- region features ----

TEST_CASE( "temporal_region_features: typed table + schema sidecar E2E",
           "[temporal][operators][features][g]" )
{
    ensureApp();
    Fixture fx;

    // Two dates × 2 pixels are not enough — the operator needs >= 3 dates.
    // Region pt trends up, field trends down.
    Json::Value params( Json::objectValue );
    Json::Value scenes( Json::arrayValue );
    const int n = 60; // ~weekly
    for ( int i = 0; i < n; ++i )
    {
        const QString name = QStringLiteral( "f%1.tif" ).arg( i, 3, 10, QLatin1Char( '0' ) );
        const double t = 7.0 * i;
        const float up = static_cast<float>( 10.0 + 0.02 * t );
        const float down = static_cast<float>( 60.0 - 0.03 * t );
        TestScene s;
        s.path = fx.filePath( name );
        s.date = QDate( 2024, 1, 1 ).addDays( 7 * i ).toString( Qt::ISODate );
        s.values = { up, down };
        REQUIRE( writeTestScene( s ) );
        scenes.append( s.path.toStdString() );
    }
    params["scenes"] = scenes;
    params["band"] = 1;
    Json::Value regions( Json::arrayValue );
    Json::Value r0( Json::objectValue );
    r0["id"] = "up";
    Json::Value p0( Json::arrayValue );
    p0.append( 500015.0 );
    p0.append( 4499985.0 );
    r0["point"] = p0;
    regions.append( r0 );
    Json::Value r1( Json::objectValue );
    r1["id"] = "down";
    Json::Value p1( Json::arrayValue );
    p1.append( 500045.0 );
    p1.append( 4499985.0 );
    r1["point"] = p1;
    regions.append( r1 );
    params["regions"] = regions;
    params["trend_method"] = "ols";
    params["change_harmonics"] = 0; // trend-only scenario: no change features
    params["cycles"] = 1;
    params["output"] = fx.filePath( QStringLiteral( "features.csv" ) ).toStdString();

    const Json::Value result = runOp( "rs:temporal_region_features", params );
    REQUIRE( result["regionCount"].asInt() == 2 );
    REQUIRE( std::string( result["schema"].asString() ).find(
                 "exp_rs_temporal_region_features/1" ) != std::string::npos );

    QFile csv( fx.filePath( "features.csv" ) );
    REQUIRE( csv.open( QIODevice::ReadOnly | QIODevice::Text ) );
    const QStringList lines = QString::fromUtf8( csv.readAll() ).trimmed().split( '\n' );
    csv.close();
    REQUIRE( lines.size() == 3 );
    const QStringList header = lines.first().split( ',' );
    REQUIRE( header.first() == "region_id" );
    REQUIRE( header.contains( "trend_slope_per_day" ) );
    REQUIRE( header.contains( "c1_sos_median" ) );
    REQUIRE( header.contains( "years_covered" ) );

    double upSlope = 0.0;
    double downSlope = 0.0;
    for ( int i = 1; i < lines.size(); ++i )
    {
        const QStringList cols = lines[i].split( ',' );
        const int slopeCol = header.indexOf( "trend_slope_per_day" );
        if ( cols[0] == "up" )
            upSlope = cols[slopeCol].toDouble();
        if ( cols[0] == "down" )
            downSlope = cols[slopeCol].toDouble();
    }
    REQUIRE( upSlope == Approx( 0.02 ).margin( 0.005 ) );
    REQUIRE( downSlope == Approx( -0.03 ).margin( 0.005 ) );

    // Sidecar exists and pins the same feature order.
    QFile sidecar( fx.filePath( "features.csv.json" ) );
    REQUIRE( sidecar.open( QIODevice::ReadOnly | QIODevice::Text ) );
    const QByteArray sidecarBytes = sidecar.readAll();
    sidecar.close();
    Json::Value doc;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream s( sidecarBytes.toStdString() );
    REQUIRE( Json::parseFromStream( b, s, &doc, &errs ) );
    REQUIRE( doc["featureNames"].size() == header.size() - 1 );
    REQUIRE( doc["missingToken"].asString() == "nan" );
}

// --------------------------------------------------------------- fusion ----
// rs:temporal_sar_fusion (Temporal Phenology 12.0, WP6): concatenates two
// already-coregistered feature stacks on ONE verified pixel grid and refuses
// mismatched grids with a typed error — it never realigns.

namespace
{
struct TestStack
{
    QString path;
    int width = 2;
    int height = 1;
    std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
    int epsg = 32648;
    /// (band name, pixel values) pairs — one raster band each.
    std::vector<std::pair<QString, std::vector<float>>> bands;
};

bool writeTestStack( const TestStack &s )
{
    ensureGdalInit();
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( s.epsg ) != OGRERR_NONE )
        return false;
    char *wktOut = nullptr;
    srs.exportToWkt( &wktOut );
    const QString wkt = QString::fromUtf8( wktOut );
    CPLFree( wktOut );

    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( driver, s.path.toUtf8().constData(), s.width, s.height,
                                  static_cast<int>( s.bands.size() ), GDT_Float32, nullptr );
    if ( !ds )
        return false;
    GDALSetGeoTransform( ds, const_cast<double *>( s.gt.data() ) );
    GDALSetProjection( ds, wkt.toUtf8().constData() );
    bool ok = true;
    for ( size_t b = 0; b < s.bands.size(); ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, static_cast<int>( b ) + 1 );
        GDALSetDescription( band, s.bands[b].first.toUtf8().constData() );
        ok = ok && GDALRasterIO( band, GF_Write, 0, 0, s.width, s.height,
                                 const_cast<float *>( s.bands[b].second.data() ),
                                 s.width, s.height, GDT_Float32, 0, 0 ) == CE_None;
    }
    GDALClose( ds );
    return ok;
}

QString bandDescription( const QString &path, int band )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    const QString d = QString::fromUtf8(
        GDALGetDescription( GDALGetRasterBand( ds, band ) ) );
    GDALClose( ds );
    return d;
}
} // namespace

TEST_CASE( "rs:temporal_sar_fusion concatenates shared-grid feature stacks "
           "with prefixed names and provenance",
           "[temporal][fusion][operators10]" )
{
    ensureApp();
    Fixture fx;
    TestStack opt;
    opt.path = fx.filePath( "optical.tif" );
    opt.bands = { { QStringLiteral( "sos" ), { 112.0f, 111.0f } },
                  { QStringLiteral( "amplitude" ), { 0.7f, 0.65f } } };
    REQUIRE( writeTestStack( opt ) );
    TestStack sar;
    sar.path = fx.filePath( "sar.tif" );
    sar.bands = { { QStringLiteral( "vv_mean" ), { -12.5f, -11.0f } } };
    REQUIRE( writeTestStack( sar ) );

    Json::Value params;
    params["optical"] = opt.path.toStdString();
    params["sar"] = sar.path.toStdString();
    params["output"] = fx.filePath( "fused.tif" ).toStdString();
    const Json::Value result = runOp( "rs:temporal_sar_fusion", params );
    REQUIRE( result["bands"].asInt() == 3 );
    REQUIRE( result["opticalBands"].asInt() == 2 );
    REQUIRE( result["sarBands"].asInt() == 1 );

    const QString out = QString::fromStdString( result["output"].asString() );
    REQUIRE( bandCount( out ) == 3 );
    REQUIRE( bandDescription( out, 1 ) == QLatin1String( "opt_sos" ) );
    REQUIRE( bandDescription( out, 2 ) == QLatin1String( "opt_amplitude" ) );
    REQUIRE( bandDescription( out, 3 ) == QLatin1String( "sar_vv_mean" ) );
    const auto b1 = readBand( out, 1 );
    REQUIRE( b1[0] == Approx( 112.0f ).margin( 1e-5 ) );
    const auto b3 = readBand( out, 3 );
    REQUIRE( b3[0] == Approx( -12.5f ).margin( 1e-5 ) );

    // Provenance metadata records both inputs and the grid contract.
    GDALDatasetH ds = GDALOpen( out.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    const char *optProv = GDALGetMetadataItem( ds, "SICNU_FUSION_OPTICAL_PATH", nullptr );
    const char *mode = GDALGetMetadataItem( ds, "SICNU_FUSION_MODE", nullptr );
    REQUIRE( optProv != nullptr );
    REQUIRE( QString::fromUtf8( optProv ).endsWith( "optical.tif" ) );
    REQUIRE( mode != nullptr );
    REQUIRE( QString::fromUtf8( mode ) == QLatin1String( "feature_stack_shared_grid" ) );
    GDALClose( ds );
}

TEST_CASE( "rs:temporal_sar_fusion refuses mismatched grids as a typed error",
           "[temporal][fusion][operators10]" )
{
    ensureApp();
    Fixture fx;
    TestStack opt;
    opt.path = fx.filePath( "optical.tif" );
    opt.bands = { { QStringLiteral( "sos" ), { 112.0f, 111.0f } } };
    REQUIRE( writeTestStack( opt ) );

    // Same CRS/extent family but a shifted origin → geotransform mismatch.
    TestStack shifted = opt;
    shifted.path = fx.filePath( "sar_shifted.tif" );
    shifted.gt = { 500000, 30, 0, 4500001, 0, -30 };
    REQUIRE( writeTestStack( shifted ) );

    // Same geotransform but different extent → dimension mismatch.
    TestStack wider = opt;
    wider.path = fx.filePath( "sar_wider.tif" );
    wider.width = 4;
    wider.bands = { { QStringLiteral( "vv_mean" ),
                      { -12.0f, -12.0f, -12.0f, -12.0f } } };
    REQUIRE( writeTestStack( wider ) );

    // Different CRS → projection mismatch.
    TestStack otherCrs = opt;
    otherCrs.path = fx.filePath( "sar_othercrs.tif" );
    otherCrs.epsg = 32649;
    REQUIRE( writeTestStack( otherCrs ) );

    for ( const QString &bad : { shifted.path, wider.path, otherCrs.path } )
    {
        Json::Value params;
        params["optical"] = opt.path.toStdString();
        params["sar"] = bad.toStdString();
        params["output"] = fx.filePath( "should_not_exist.tif" ).toStdString();
        auto op = RSOperatorRegistry::instance().create( "rs:temporal_sar_fusion" );
        REQUIRE( op != nullptr );
        RSOperatorContext ctx;
        REQUIRE_THROWS( op->run( params, ctx ) );
        // Refusal must not leave a partial output behind.
        REQUIRE( !QFile::exists( fx.filePath( "should_not_exist.tif" ) ) );
    }
}
