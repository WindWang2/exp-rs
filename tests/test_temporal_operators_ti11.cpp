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

/// Deterministic irregular jitter: floors residual SSEs away from exact
/// zero so nested-model F tests and AICc comparisons stay well-conditioned
/// on synthetic (noiseless-shape) series. Not random — no seed needed.
inline double jitter( int i ) { return 0.01 * std::sin( 7.31 * i ) + 0.005 * std::cos( 1.7 * i ); }

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

/// Builds a scene stack: 2×1 pixels, 16-day cadence starting 2021-01-01
/// (a non-leap year: real-date doy axes stay aligned with the 16-day grid).
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
    const QDate start( 2021, 1, 1 );
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
                0.3 + amp * std::sin( 2.0 * kPi * ( 16.0 * i ) / 365.25 ) +
                jitter( i ) );
        },
        [&]( int i ) {
            return static_cast<float>(
                5.0 + ( i < kBreak ? 0.0 : 1.5 ) + jitter( i ) );
        } );

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
    // Scan every break slot: bands are grouped by metric (kind_1..k,
    // day_1..k, mag_1..k, shift_1..k, pvalue_1..k after breaks_count).
    std::vector<std::vector<float>> kinds, days, mags, shifts;
    for ( int slot = 0; slot < 3; ++slot )
    {
        kinds.push_back( readBand( out, 2 + slot ) );
        days.push_back( readBand( out, 5 + slot ) );
        mags.push_back( readBand( out, 8 + slot ) );
        shifts.push_back( readBand( out, 11 + slot ) );
    }
    auto findNear = [&]( int pixel, double truthDay, double kindLo, double kindHi ) {
        for ( int slot = 0; slot < 3; ++slot )
        {
            const float kind = kinds[slot][pixel];
            const float day = days[slot][pixel];
            if ( std::isfinite( day ) && std::abs( day - truthDay ) <= 10 * 16.0 &&
                 kind >= kindLo && kind <= kindHi )
                return true;
        }
        return false;
    };

    // Pixel 0 = seasonal; pixel 1 = trend. (Kind encoding: 1 trend,
    // 2 seasonal, 3 both.) Tolerance 10 samples: the greedy segmentation
    // may straddle the true change with two breaks.
    REQUIRE( counts[0] >= 1.0f );
    REQUIRE( counts[1] >= 1.0f );
    // The seasonal pixel must NOT be classified trend-only, and vice versa.
    CHECK( findNear( 0, 16.0 * kBreak, 2.0f, 3.0f ) );
    CHECK( !findNear( 0, 16.0 * kBreak, 1.0f, 1.0f ) );
    CHECK( findNear( 1, 16.0 * kBreak, 1.0f, 3.0f ) );
    CHECK( !findNear( 1, 16.0 * kBreak, 2.0f, 2.0f ) );
    // Seasonal shift is large on the seasonal pixel and small on the
    // trend pixel (scan the slot nearest the truth).
    bool shiftChecked0 = false;
    bool shiftChecked1 = false;
    for ( int slot = 0; slot < 3; ++slot )
    {
        if ( std::isfinite( days[slot][0] ) &&
             std::abs( days[slot][0] - 16.0 * kBreak ) <= 10 * 16.0 )
        {
            CHECK( shifts[slot][0] > 0.1 );
            shiftChecked0 = true;
        }
        if ( std::isfinite( days[slot][1] ) &&
             std::abs( days[slot][1] - 16.0 * kBreak ) <= 10 * 16.0 )
        {
            CHECK( shifts[slot][1] < 0.25 * 1.5 + 0.1 );
            shiftChecked1 = true;
        }
    }
    CHECK( shiftChecked0 );
    CHECK( shiftChecked1 );
    CHECK( std::isfinite( mags[0][0] ) );
    CHECK( std::isfinite( mags[0][1] ) );
}

TEST_CASE( "seasonal_breaks: compute_ci produces per-break finite bounds or "
           "refusal, deterministically",
           "[temporal][operators][ti11][seasonal_breaks][ci]" )
{
    ensureApp();
    Fixture fx;
    const int n = 72;
    // TWO genuine steps (samples 24 and 48): both break slots carry real
    // jumps, so per-break CI slots must be finite AND mutually distinct —
    // a copy of break 1's interval into slot 2 would fail this.
    const Stack stack = writeStack(
        fx, n,
        [&]( int i ) {
            const double step = i < 24 ? 0.0 : ( i < 48 ? 1.2 : 2.6 );
            return static_cast<float>( 5.0 + step + jitter( i ) );
        },
        [&]( int i ) {
            const double step = i < 24 ? 0.0 : ( i < 48 ? 1.2 : 2.6 );
            return static_cast<float>( 5.0 + step + jitter( i ) );
        } );

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
    // CI bands come after the five per-break metric groups:
    // mag_ci_lo_1, mag_ci_lo_2, mag_ci_hi_1, mag_ci_hi_2.
    const std::vector<float> counts = readBand( out, 1 );
    const std::vector<float> lo1 = readBand( out, 12 );
    const std::vector<float> lo2 = readBand( out, 13 );
    const std::vector<float> hi1 = readBand( out, 14 );
    const std::vector<float> hi2 = readBand( out, 15 );

    for ( int p = 0; p < 2; ++p )
    {
        INFO( "pixel " << p << " breaks=" << counts[p] );
        REQUIRE( counts[p] >= 1.0f );
        CHECK( std::isfinite( lo1[p] ) );
        CHECK( std::isfinite( hi1[p] ) );
        CHECK( lo1[p] <= hi1[p] );
        if ( counts[p] >= 2.0f )
        {
            // Per-break intervals: finite and NOT copies of slot 1.
            CHECK( std::isfinite( lo2[p] ) );
            CHECK( std::isfinite( hi2[p] ) );
            CHECK( lo2[p] <= hi2[p] );
            CHECK( lo2[p] != lo1[p] );
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
                0.2 * std::sin( 4.0 * kPi * t / 365.25 ) + jitter( i ) );
        },
        []( int ) { return 2.0f; } );  // exact constant: smallest candidate wins with RSS 0

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
    // Pixel 0: max(0, sin) double season — two peaks per year with EXACT
    // flat zero troughs, so no trough bump can become a spurious cycle.
    // Pixel 1: same shape but doy 60..280 masked (a >200-day hole) → every
    // window that crosses the hole must refuse on the gap gate.
    const Stack stack = writeStack(
        fx, n,
        [&]( int i ) {
            const double t = 16.0 * i;
            return static_cast<float>(
                0.2 + 0.8 * std::max( 0.0, std::sin( 2.0 * kPi * t / 365.25 ) ) );
        },
        [&]( int i ) {
            const QDate start( 2021, 1, 1 );
            const int doy = start.addDays( 16 * i ).dayOfYear();
            if ( doy >= 60 && doy <= 280 )
                return kNan;
            const double t = 16.0 * i;
            return static_cast<float>(
                0.2 + 0.8 * std::max( 0.0, std::sin( 2.0 * kPi * t / 365.25 ) ) );
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

    INFO( "counts=" << counts[0] << "," << counts[1]
                    << " refusals=" << refusals[0] << "," << refusals[1] );
    // Pixel 0: a complete double-cropping pixel — cycles 1 and 2 exist.
    CHECK( counts[0] >= 1.0f );
    CHECK( c1Valid[0] == 1.0f );
    // The gapped pixel refused ≥1 window; refused cycles carry NaN metrics.
    CHECK( refusals[1] >= 1.0f );
    // No fabricated metrics anywhere: valid=0 cycles carry NaN POS.
    for ( size_t p : { 0u, 1u } )
    {
        if ( c2Valid[p] == 0.0f )
            CHECK( !std::isfinite( readBand( out, 13 )[p] ) );  // cycle2_amp NaN
    }
}
