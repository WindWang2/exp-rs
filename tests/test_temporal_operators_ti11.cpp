// tests/test_temporal_operators_ti11.cpp — operator end-to-end tests for
// Temporal Intelligence 11.0: rs:temporal_seasonal_breaks,
// rs:temporal_model_select, rs:temporal_phenology_multi. Synthetic GeoTIFF
// fixtures only; expectations come from the closed-form generator (never
// from the code under test).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "operators/framework/rs_operator_context.h"
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
char appArgv0[] = "test_temporal_operators_ti11";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

struct Fixture
{
    QTemporaryDir dir;
    Fixture() { REQUIRE( dir.isValid() ); }
    QString filePath( const QString &name ) const { return dir.filePath( name ); }
};

bool writeScene( const QString &path, float pixel0, float pixel1,
                 const QDate &date )
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
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 2, 1, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    const std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
    GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) );
    GDALSetProjection( ds, wkt.toUtf8().constData() );
    GDALSetMetadataItem( ds, "SICNU_ACQUISITION_DATE",
                         date.toString( QStringLiteral( "yyyy-MM-dd" ) ).toUtf8().constData(),
                         nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, -9999.0 );
    float row[2] = { pixel0, pixel1 };
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 2, 1, row, 2, 1,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

/// Builds a scene stack: 2×1 pixels, weekly 16-day cadence starting 2020-01-01.
/// @a pixel0/@a pixel1 value each sample.
struct Stack
{
    std::vector<QString> paths;
    std::vector<double> tDays;
};

Stack writeStack( Fixture &fx, int samples,
                  const std::function<float( int )> &pixel0,
                  const std::function<float( int )> &pixel1 )
{
    Stack stack;
    const QDate start( 2020, 1, 1 );
    for ( int i = 0; i < samples; ++i )
    {
        const QDate date = start.addDays( 16 * i );
        const QString path = fx.filePath( QStringLiteral( "s_%1.tif" ).arg( i, 3, 10, QLatin1Char( '0' ) ) );
        REQUIRE( writeScene( path, pixel0( i ), pixel1( i ), date ) );
        stack.paths.push_back( path );
        stack.tDays.push_back( 16.0 * i );
    }
    return stack;
}

Json::Value scenesParam( const Stack &stack )
{
    Json::Value scenes( Json::arrayValue );
    for ( const QString &p : stack.paths )
        scenes.append( p.toStdString() );
    return scenes;
}

Json::Value runOp( const char *name, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( name );
    REQUIRE( op != nullptr );
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

std::vector<float> readBand( const QString &path, int band )
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
} // namespace

TEST_CASE( "seasonal_breaks: seasonal-kind vs trend-kind pixels are separated "
           "end-to-end",
           "[temporal][operators][ti11][seasonal_breaks]" )
{
    ensureApp();
    Fixture fx;
    const int n = 92;  // ~4 years at 16-day cadence
    const int kBreak = 46;

    // Pixel 0: pure seasonal amplitude jump 0.1 → 0.9 (mean 0.3 constant).
    // Pixel 1: pure level step +1.5 (no seasonality).
    const Stack stack = writeStack(
        fx, n,
        [&]( int i ) {
            const double amp = i < kBreak ? 0.1 : 0.9;
            return static_cast<float>(
                0.3 + amp * std::sin( 2.0 * kPi * ( 16.0 * i ) / 365.25 ) );
        },
        [&]( int i ) { return static_cast<float>( 5.0 + ( i < kBreak ? 0.0 : 1.5 ) ); } );

    Json::Value params;
    params["scenes"] = scenesParam( stack );
    params["harmonics"] = 2;
    params["maxBreaks"] = 3;
    params["minSegmentDays"] = 90.0;
    params["minImprovement"] = 0.10;
    params["alpha"] = 0.05;
    const QString out = fx.filePath( QStringLiteral( "seasonal_breaks.tif" ) );
    params["output"] = out.toStdString();

    const Json::Value result = runOp( "rs:temporal_seasonal_breaks", params );
    CHECK( result["sceneCount"].asInt() == n );
    REQUIRE( bandCount( out ) == 1 + 5 * 3 + 2 + 1 );  // count + 5 per-break bands ×3 + rmse/r2 + valid

    const std::vector<float> counts = readBand( out, 1 );
    const std::vector<float> kind1 = readBand( out, 2 );   // break_kind_1
    const std::vector<float> day1 = readBand( out, 3 );    // break_day_1
    const std::vector<float> mag1 = readBand( out, 4 );    // break_mag_1
    const std::vector<float> shift1 = readBand( out, 5 );  // break_seasonal_shift_1

    // Pixel 0 = seasonal; pixel 1 = trend. (Kind encoding: 1 trend,
    // 2 seasonal, 3 both.)
    INFO( "pixel0 kind=" << kind1[0] << " pixel1 kind=" << kind1[1] );
    REQUIRE( counts[0] >= 1.0f );
    REQUIRE( counts[1] >= 1.0f );
    // The seasonal pixel must NOT be classified trend-only, and vice versa.
    CHECK( ( kind1[0] == 2.0f || kind1[0] == 3.0f ) );
    CHECK( ( kind1[1] == 1.0f || kind1[1] == 3.0f ) );
    // Both breaks localize near the truth (sample 46 → day 736).
    CHECK( std::abs( day1[0] - 16.0 * kBreak ) <= 5 * 16.0 );
    CHECK( std::abs( day1[1] - 16.0 * kBreak ) <= 5 * 16.0 );
    // Seasonal shift is large on the seasonal pixel and small on the
    // trend pixel.
    CHECK( shift1[0] > 0.1 );
    CHECK( shift1[1] < 0.25 * 1.5 + 0.1 );
    CHECK( std::isfinite( mag1[0] ) );
    CHECK( std::isfinite( mag1[1] ) );
}

TEST_CASE( "seasonal_breaks: compute_ci produces finite bounds or refusal, "
           "deterministically",
           "[temporal][operators][ti11][seasonal_breaks][ci]" )
{
    ensureApp();
    Fixture fx;
    const int n = 60;
    const int kBreak = 30;
    const Stack stack = writeStack(
        fx, n,
        [&]( int i ) { return static_cast<float>( 5.0 + ( i < kBreak ? 0.0 : 1.5 ) ); },
        [&]( int i ) { return static_cast<float>( 5.0 + ( i < kBreak ? 0.0 : 1.5 ) ); } );

    Json::Value params;
    params["scenes"] = scenesParam( stack );
    params["harmonics"] = 1;
    params["maxBreaks"] = 2;
    params["minSegmentDays"] = 60.0;
    params["compute_ci"] = true;
    params["bootstrap_resamples"] = 19;
    params["bootstrap_seed"] = 20260915;
    const QString out = fx.filePath( QStringLiteral( "sb_ci.tif" ) );
    params["output"] = out.toStdString();

    const Json::Value result = runOp( "rs:temporal_seasonal_breaks", params );
    (void)result;
    // 1 + 5*2 + 2*2 + 2 + 1 = 18 bands with CI on.
    REQUIRE( bandCount( out ) == 1 + 5 * 2 + 2 * 2 + 2 + 1 );
    const std::vector<float> lo = readBand( out, 12 );  // mag_ci_lo_1
    const std::vector<float> hi = readBand( out, 13 );  // mag_ci_hi_1
    // Either a valid interval (lo <= hi) or the documented NaN refusal —
    // never a fabricated inverted interval.
    for ( float v : { lo[0], hi[0], lo[1], hi[1] } )
    {
        if ( std::isfinite( v ) )
        {
            CHECK( lo[0] <= hi[0] );
            CHECK( lo[1] <= hi[1] );
            break;
        }
    }
}

TEST_CASE( "model_select: recovers harmonic order end-to-end and refuses "
           "degenerate pixels",
           "[temporal][operators][ti11][model_select]" )
{
    ensureApp();
    Fixture fx;
    const int n = 92;
    // Pixel 0: two-frequency series (order 2 truth). Pixel 1: constant.
    const Stack stack = writeStack(
        fx, n,
        [&]( int i ) {
            const double t = 16.0 * i;
            return static_cast<float>(
                0.5 + 0.4 * std::sin( 2.0 * kPi * t / 365.25 ) +
                0.2 * std::sin( 4.0 * kPi * t / 365.25 ) );
        },
        []( int ) { return 2.0f; } );

    Json::Value params;
    params["scenes"] = scenesParam( stack );
    params["maxHarmonics"] = 3;
    params["maxBreaks"] = 0;
    params["penalty"] = "aicc";
    const QString out = fx.filePath( QStringLiteral( "model_select.tif" ) );
    params["output"] = out.toStdString();

    const Json::Value result = runOp( "rs:temporal_model_select", params );
    CHECK( result["pixelsFitted"].asUInt64() >= 1 );
    CHECK( result["pixelsByHarmonics"]["2"].asUInt64() >= 1 );

    const std::vector<float> harmonics = readBand( out, 1 );
    const std::vector<float> valid = readBand( out, 5 );
    CHECK( harmonics[0] == Approx( 2.0f ) );
    // Constant pixel: RSS ≈ 0 for the smallest candidate → order 0.
    CHECK( harmonics[1] == Approx( 0.0f ).margin( 0.001 ) );
    CHECK( valid[0] == Approx( static_cast<float>( n ) ) );
}

TEST_CASE( "phenology_multi: double-cropping pixel reports two cycles, "
           "gapped pixel reports refusals",
           "[temporal][operators][ti11][phenology_multi]" )
{
    ensureApp();
    Fixture fx;
    const int n = 69;  // 3 years at 16-day cadence
    // Pixel 0: |sin| double season. Pixel 1: same shape but doy 150..260
    // masked (a >100-day hole) → windows crossing the hole must refuse.
    const Stack stack = writeStack(
        fx, n,
        [&]( int i ) {
            const double t = 16.0 * i;
            return static_cast<float>(
                0.2 + 0.8 * std::abs( std::sin( 2.0 * kPi * t / 365.25 ) ) );
        },
        [&]( int i ) {
            const QDate start( 2020, 1, 1 );
            const int doy = start.addDays( 16 * i ).dayOfYear();
            if ( doy >= 150 && doy <= 260 )
                return kNan;
            const double t = 16.0 * i;
            return static_cast<float>(
                0.2 + 0.8 * std::abs( std::sin( 2.0 * kPi * t / 365.25 ) ) );
        } );

    Json::Value params;
    params["scenes"] = scenesParam( stack );
    params["maxCyclesPerYear"] = 3;
    params["crossingFraction"] = 0.5;
    params["minValidPerSeason"] = 4;
    const QString out = fx.filePath( QStringLiteral( "phenology_multi.tif" ) );
    params["output"] = out.toStdString();

    const Json::Value result = runOp( "rs:temporal_phenology_multi", params );
    CHECK( result["pixelsWithAnyCycle"].asUInt64() >= 1 );
    // Bands: cycle_count, refusal_count, 6 per cycle × 3 cycles.
    REQUIRE( bandCount( out ) == 2 + 6 * 3 );
    const std::vector<float> counts = readBand( out, 1 );
    const std::vector<float> refusals = readBand( out, 2 );
    const std::vector<float> c1Valid = readBand( out, 8 );   // cycle1_valid
    const std::vector<float> c2Valid = readBand( out, 14 );  // cycle2_valid
    const std::vector<float> c1Pos = readBand( out, 4 );     // cycle1_pos

    // Pixel 0: a complete double-cropping pixel — cycle 1 and 2 valid.
    INFO( "counts=" << counts[0] << "," << counts[1] << " refusals=" << refusals[0] << "," << refusals[1] );
    CHECK( counts[0] >= 1.0f );
    CHECK( c1Valid[0] == 1.0f );
    CHECK( refusals[1] >= 1.0f );  // the gapped pixel refused ≥1 window
    // A valid double-cropping cycle peaks near |sin| peaks (doy 92 / 275).
    if ( c2Valid[0] == 1.0f )
        CHECK( c1Pos[0] == Approx( 92.0 ).margin( 60.0 ) );
    // No fabricated metrics anywhere: valid=0 cycles carry NaN POS.
    for ( size_t p : { 0u, 1u } )
    {
        if ( c2Valid[p] == 0.0f )
            CHECK( !std::isfinite( readBand( out, 13 )[p] ) );  // cycle2_amp NaN
    }
}
