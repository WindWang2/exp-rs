// tests/test_d16_temporal_phenology_e2e.cpp — D16 Package I: end-to-end
// chain (VirtualCube -> 16-day regularization -> robust Whittaker -> BFAST
// -> phenology -> agent diagnosis) plus the Lab08 100-point auto-grading.
// Truth: the analytic season model below; every hop's expected values are
// closed-form or winner-arithmetic on the scene geometry — never derived
// from the code under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "agent/spatial_tools/temporal_spatial_tools.h"
#include "core/temporal_cube.h"
#include "processing/algorithms/breakpoint_detection.h"
#include "processing/algorithms/phenology_metrics.h"
#include "processing/algorithms/temporal_smoothing.h"

#include <gdal_priv.h>
#include <cpl_conv.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>

#include <sys/resource.h>

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using Catch::Approx;
using sicnu::temporal::BreakpointDetector;
using sicnu::temporal::PhenologyExtractor;
using sicnu::temporal::TemporalCube;
using sicnu::temporal::TemporalCubeConfig;

namespace
{

constexpr double kPeriodDays = 365.0;   // scenario year length (whole days)
constexpr int kScenes = 46;             // 16-day cadence, 2 years (t = 0..720)
constexpr int kGrid = 48;
constexpr int kCloudX = 24;             // cloud half-plane x < 24

// Seasonal truth: Gaussian season per year + deforestation step at the
// year-2 boundary (intercept 0.45 -> 0.10).
double truth( double t )
{
    const double base = t < kPeriodDays ? 0.45 : 0.10;
    const double mu = 140.0 + ( t >= kPeriodDays ? kPeriodDays : 0.0 );
    const double u = ( t - mu ) / 60.0; // wide season: resolvable at 16-day sampling
    return base + 0.3 * std::exp( -0.5 * u * u );
}

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_d16_temporal_phenology_e2e";
char *appArgv[] = { appArgv0, nullptr };

QApplication &ensureApp()
{
    if ( !QApplication::instance() )
        new QApplication( appArgc(), appArgv );
    return *qobject_cast<QApplication *>( QApplication::instance() );
}

/// Disk-backed fixture directory (removed on scope exit).
struct FixtureDir
{
    FixtureDir()
    {
        const QString root = QStringLiteral( CMAKE_BINARY_DIR ) + QStringLiteral( "/d16_e2e_fixtures" );
        QDir().mkpath( root );
        const QString unique = root + QStringLiteral( "/%1" ).arg(
            reinterpret_cast<quintptr>( this ), 0, 16 );
        QDir().mkpath( unique );
        mPath = unique;
    }
    ~FixtureDir() { QDir( mPath ).removeRecursively(); }
    QDir dir() const { return QDir( mPath ); }
    QString mPath;
};

struct SceneFactory
{
    QDir dir;
    std::vector<QString> paths;

    /// 46 scenes on a 16-day axis; even scenes carry an opaque cloud
    /// half-plane (band 2 = 1 on x < 24). Values are the exact analytic
    /// truth (no noise).
    static SceneFactory build( const QDir &fixtureDir )
    {
        GDALAllRegister();
        SceneFactory factory{ fixtureDir, {} };
        GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
        std::vector<float> values( static_cast<std::size_t>( kGrid ) * kGrid );
        std::vector<float> cloud( static_cast<std::size_t>( kGrid ) * kGrid, 0.0f );
        const QDate epoch( 2020, 1, 1 );
        for ( int k = 0; k < kScenes; ++k )
        {
            const double t = 16.0 * k;
            const QDate sceneDate = epoch.addDays( 16 * k ); // real calendar dates
            for ( int y = 0; y < kGrid; ++y )
                for ( int x = 0; x < kGrid; ++x )
                    values[static_cast<std::size_t>( y ) * kGrid + x] = static_cast<float>( truth( t ) );
            values[static_cast<std::size_t>( 40 ) * kGrid + 40] =
                static_cast<float>( truthHarmonic( t ) );
            values[static_cast<std::size_t>( 40 ) * kGrid + 30] =
                static_cast<float>( truthNoStep( t ) );
            const QString path = fixtureDir.filePath(
                QStringLiteral( "s_%1.tif" ).arg( sceneDate.toString( QStringLiteral( "yyyyMMdd" ) ) ) );
            GDALDataset *ds = driver->Create( path.toUtf8().constData(), kGrid, kGrid, 2,
                                              GDT_Float32, nullptr );
            REQUIRE( ds != nullptr );
            if ( k % 2 == 0 )
                for ( int y = 0; y < kGrid; ++y )
                    for ( int x = 0; x < kCloudX; ++x )
                        cloud[static_cast<std::size_t>( y ) * kGrid + x] = 1.0f;
            else
                std::fill( cloud.begin(), cloud.end(), 0.0f );
            REQUIRE( ds->GetRasterBand( 1 )->RasterIO(
                         GF_Write, 0, 0, kGrid, kGrid, values.data(), kGrid, kGrid, GDT_Float32, 0,
                         0, nullptr ) == CE_None );
            REQUIRE( ds->GetRasterBand( 2 )->RasterIO(
                         GF_Write, 0, 0, kGrid, kGrid, cloud.data(), kGrid, kGrid, GDT_Float32, 0,
                         0, nullptr ) == CE_None );
            GDALClose( ds );
            factory.paths.push_back( path );
        }
        return factory;
    }

    /// The documented BestPixel winner day for the clouded pixel at node k:
    /// the node's own scene when clean; on a tie the lowest scene index.
        /// Harmonic-truth pixel (40, 40): exactly representable by the BFAST
    /// design matrix [1, t, sin/cos x2], so the breakpoint magnitude is
    /// closed-form exact.
    static double truthHarmonic( double t )
    {
        const double base = t < kPeriodDays ? 0.45 : 0.10;
        return base + 0.15 * std::sin( 2.0 * M_PI * t / 365.25 ) +
               0.08 * std::cos( 4.0 * M_PI * t / 365.25 );
    }

    /// Step-free stable-forest pixel (30, 40): the phenology hop needs a
    /// series whose baseline does not change across the year boundary
    /// (smoothing across a regime step would sag the year-1 tail).
    static double truthNoStep( double t )
    {
        const double mu = 140.0 + ( t >= kPeriodDays ? kPeriodDays : 0.0 );
        const double u = ( t - mu ) / 60.0;
        return 0.45 + 0.3 * std::exp( -0.5 * u * u );
    }

int winnerDayIndex( int node, int x ) const
    {
        if ( x >= kCloudX )
            return node; // never clouded: the on-node observation wins
        const int bestOwn = ( node % 2 != 0 ) ? node : -1; // odd scenes are clean
        if ( bestOwn >= 0 )
            return bestOwn;
        for ( int d = 1; d <= 2; ++d ) // window is +-32 days = +-2 scenes
        {
            if ( node - d >= 0 && ( node - d ) % 2 != 0 )
                return node - d;
            if ( node + d < kScenes && ( node + d ) % 2 != 0 )
                return node + d;
        }
        return -1; // gap guard: NaN
    }
};

/// Edge-aware closed-form threshold crossings for the year-1 slice of the
/// wide season: the window [0, 352] does not reach the asymptotic baseline,
/// so zmin is the analytic value at the window edge and the crossing level
/// follows from the documented ratio definition.
struct CrossingTruth
{
    double sos = 0.0;
    double eos = 0.0;
    CrossingTruth( double mu, double sigma, double base, double amp, double f )
    {
        const double edge = 22.0 * 16.0; // t = 352, the last year-1 node
        const double zmin = base + amp * std::exp( -0.5 * std::pow( ( edge - mu ) / sigma, 2 ) );
        const double zmax = base + amp;
        const double yCross = zmin + f * ( zmax - zmin );
        const double fraction = ( yCross - base ) / amp;
        const double c = sigma * std::sqrt( -2.0 * std::log( fraction ) );
        sos = mu - c;
        eos = mu + c;
    }
};

std::size_t peakRssBytes()
{
    rusage usage;
    getrusage( RUSAGE_SELF, &usage );
    return static_cast<std::size_t>( usage.ru_maxrss ) * 1024ull;
}

} // namespace

TEST_CASE( "Synthetic cube factory feeds the full pipeline (tracer)", "[d16][e2e]" )
{
    ensureApp();
    FixtureDir fixtures;
    const SceneFactory factory = SceneFactory::build( fixtures.dir() );
    REQUIRE( factory.paths.size() == kScenes );

    QString why;
    auto cube = TemporalCube::open( factory.paths, TemporalCubeConfig{}, &why );
    INFO( why.toStdString() );
    REQUIRE( cube != nullptr );
    REQUIRE( cube->sliceCount() == kScenes ); // aligned 16-day scene grid
    REQUIRE( cube->width() == kGrid );

    // Calendar: nodes at exact multiples of 16 days, ISO dates from the
    // QDate oracle.
    const auto tl = cube->timeline();
    const QDate epoch( 2020, 1, 1 );
    for ( std::size_t k = 0; k < tl.size(); ++k )
    {
        REQUIRE( tl[k].tDays == Approx( 16.0 * static_cast<double>( k ) ) );
        REQUIRE( tl[k].isoDate == epoch.addDays( static_cast<int>( 16 * k ) )
                                      .toString( QStringLiteral( "yyyy-MM-dd" ) ) );
    }
}

TEST_CASE( "Full chain: cube -> smooth -> breaks -> phenology -> agent, no drift",
           "[d16][e2e]" )
{
    ensureApp();
    FixtureDir fixtures;
    const SceneFactory factory = SceneFactory::build( fixtures.dir() );
    auto cube = TemporalCube::open( factory.paths, TemporalCubeConfig{} );
    REQUIRE( cube != nullptr );
    const int n = cube->sliceCount();

    // Hop 1: cube regularization at the clean pixel is the exact truth —
    // dimensional continuity (lengths + axis) and zero fake interpolation.
    const auto series = cube->extractPixelSeries( 40, 10 );
    REQUIRE( series.size() == static_cast<std::size_t>( n ) );
    const auto tl = cube->timeline();
    for ( int k = 0; k < n; ++k )
    {
        REQUIRE( std::isfinite( series[static_cast<std::size_t>( k )] ) );
        REQUIRE( series[static_cast<std::size_t>( k )] ==
                 Approx( truth( tl[static_cast<std::size_t>( k )].tDays ) ).margin( 1e-5 ) );
    }

    // Hop 2: robust Whittaker on a 30%-spiked variant hugs the clean truth.
    std::vector<float> spiked( series );
    for ( std::size_t i = 2; i < spiked.size(); i += 5 ) // isolated cloud hits
        spiked[i] -= 0.35f;
    const auto smoothed = sicnu::temporal::d16::whittakerSmoothRobust( spiked, {}, 10.0, 4 );
    REQUIRE( smoothed.size() == series.size() );
    double mae = 0.0;
    for ( std::size_t i = 0; i < smoothed.size(); ++i )
        mae += std::abs( smoothed[i] - series[i] );
    mae /= smoothed.size();
    // Sampling floor: even the clean input smooths to ~0.026 MAE at this
    // geometry (16-day axis, sigma = 60 d season), so the honest robust
    // bound is 0.03 — while the plain smoother stays at 0.07+.
    const auto plain = sicnu::temporal::d16::whittakerSmooth( spiked, {}, 10.0, 2 );
    double plainMae = 0.0;
    for ( std::size_t i = 0; i < series.size(); ++i )
        plainMae += std::abs( plain[i] - series[i] );
    plainMae /= series.size();
    INFO( "robust MAE = " << mae << " plain = " << plainMae );
    REQUIRE( mae < 0.03 );
    REQUIRE( plainMae > 2.0 * mae );

    // Hop 3: BFAST on the harmonic pixel — the model is exactly
    // representable, so the break index and the -0.35 level jump are exact.
    // (The Gaussian season is NOT harmonic-representable; running the
    // detector on it would report model artifacts, not land-cover change.)
    std::vector<double> t( static_cast<std::size_t>( n ) );
    for ( int k = 0; k < n; ++k )
        t[static_cast<std::size_t>( k )] = tl[static_cast<std::size_t>( k )].tDays;
    const auto harmonicSeries = cube->extractPixelSeries( 40, 40 );
    REQUIRE( harmonicSeries.size() == series.size() );
    for ( std::size_t i = 0; i < harmonicSeries.size(); ++i )
        REQUIRE( harmonicSeries[i] ==
                 Approx( SceneFactory::truthHarmonic( t[i] ) ).margin( 1e-4 ) );
    const auto harmonicBreaks =
        BreakpointDetector::detectHarmonicBreaks( harmonicSeries, t, 2, 2, 10, 0.05 );
    REQUIRE( harmonicBreaks.valid );
    REQUIRE( harmonicBreaks.breakCount == 1 );
    REQUIRE( harmonicBreaks.breakpoints[0].index == 23 );
    REQUIRE( harmonicBreaks.breakpoints[0].magnitude == Approx( -0.35 ).margin( 1e-3 ) );
    REQUIRE( harmonicBreaks.breakpoints[0].pValue < 0.01 );

    // Hop 4: phenology on the step-free pixel's year-1 slice against the
    // edge-aware closed-form crossings.
    const auto stableSeries = cube->extractPixelSeries( 30, 40 );
    std::vector<float> year1( stableSeries.begin(), stableSeries.begin() + 23 );
    std::vector<double> t1( t.begin(), t.begin() + 23 );
    const auto phen = PhenologyExtractor::extractDynamicThreshold( year1, t1, 0.2, 1, 365 );
    REQUIRE( phen.valid );
    const CrossingTruth truth1( 140.0, 60.0, 0.45, 0.3, 0.2 );
    INFO( "sos=" << phen.sos << " pos=" << phen.pos << " eos=" << phen.eos );
    REQUIRE( phen.sos == Approx( truth1.sos ).margin( 2.5 ) );
    REQUIRE( phen.pos == Approx( 140.0 ).margin( 5.0 ) ); // 16-day sample grid
    REQUIRE( phen.eos == Approx( truth1.eos ).margin( 2.5 ) );
    REQUIRE( phen.sos < phen.pos );
    REQUIRE( phen.pos < phen.eos );

    // Hop 5: the agent tool consumes the same chain (seasonal monthly means)
    // and answers with honest, ordered phenology.
    sicnu::agent::TemporalSpatialTool::clearSeries();
    std::vector<float> monthly;
    for ( int m = 0; m < 12; ++m )
    {
        const double doy = 15.5 + 30.4 * m;
        monthly.push_back( static_cast<float>( truth( doy ) ) );
    }
    sicnu::agent::TemporalSpatialTool::ingestSeries( "NDVI", 104.06, 30.67, 2020, monthly );
    Json::Value args;
    args["lon"] = 104.06;
    args["lat"] = 30.67;
    args["metric"] = "NDVI";
    args["year"] = 2020;
    const Json::Value diagnosis =
        sicnu::agent::TemporalSpatialTool::executeTool( "temporal:phenology_query", args );
    REQUIRE( diagnosis["status"].asString() == "success" );
    REQUIRE( diagnosis["sos"].asDouble() < diagnosis["pos"].asDouble() );
    REQUIRE( diagnosis["pos"].asDouble() < diagnosis["eos"].asDouble() );
}

// ---------------------------------------------------------------------------
// Slice 3: Lab08 auto-grading — 4 x 25 points, headless, RSS-bounded.
// ---------------------------------------------------------------------------

namespace
{
struct GradingItem
{
    std::string name;
    int score = 0;
    int max = 25;
};
} // namespace

TEST_CASE( "Lab08 auto-grading reaches the 100-point baseline", "[d16][e2e][lab08]" )
{
    ensureApp();
    FixtureDir fixtures;
    const SceneFactory factory = SceneFactory::build( fixtures.dir() );
    auto cube = TemporalCube::open( factory.paths, TemporalCubeConfig{} );
    REQUIRE( cube != nullptr );
    const int n = cube->sliceCount();
    const auto tl = cube->timeline();
    std::vector<double> t( static_cast<std::size_t>( n ) );
    for ( int k = 0; k < n; ++k )
        t[static_cast<std::size_t>( k )] = tl[static_cast<std::size_t>( k )].tDays;

    std::vector<GradingItem> items;

    // Item 1 (25): regularization + cloud masking correctness — every node
    // of the clouded pixel equals the winning observation's truth.
    {
        const auto series = cube->extractPixelSeries( 10, 10 );
        int ok = 0;
        for ( int k = 0; k < n; ++k )
        {
            const int winner = factory.winnerDayIndex( k, 10 );
            if ( winner < 0 )
            {
                if ( std::isnan( series[static_cast<std::size_t>( k )] ) )
                    ++ok;
                continue;
            }
            if ( series[static_cast<std::size_t>( k )] ==
                 Approx( truth( 16.0 * winner ) ).margin( 1e-4 ) )
                ++ok;
        }
        items.push_back( { "regularization+cloud", ok == n ? 25 : 0, 25 } );
        INFO( "item1 ok=" << ok << "/" << n );
    }

    // Item 2 (25): robust Whittaker convergence under cloud-spike geometry.
    {
        const auto series = cube->extractPixelSeries( 40, 10 );
        std::vector<float> spiked( series );
        for ( std::size_t i = 2; i < spiked.size(); i += 5 ) // isolated cloud hits
            spiked[i] -= 0.35f;
        const auto smoothed = sicnu::temporal::d16::whittakerSmoothRobust( spiked, {}, 10.0, 4 );
        double mae = 0.0;
        for ( std::size_t i = 0; i < smoothed.size(); ++i )
            mae += std::abs( smoothed[i] - series[i] );
        mae /= smoothed.size();
        INFO( "item2 mae=" << mae );
        items.push_back( { "whittaker", mae < 0.03 ? 25 : 0, 25 } );
    }

    // Item 3 (25): phenology SOS/POS/EOS against closed-form crossings.
    {
        const auto series = cube->extractPixelSeries( 40, 10 );
        std::vector<float> year1( series.begin(), series.begin() + 23 );
        const auto phen =
            PhenologyExtractor::extractDynamicThreshold( year1, std::vector<double>( t.begin(), t.begin() + 23 ), 0.2, 1, 365 );
        const CrossingTruth truth1( 140.0, 60.0, 0.45, 0.3, 0.2 );
        const bool ok = phen.valid &&
                        std::abs( phen.sos - truth1.sos ) <= 2.5 &&
                        std::abs( phen.pos - 140.0 ) <= 5.0 &&
                        std::abs( phen.eos - truth1.eos ) <= 2.5;
        INFO( "item3 sos=" << phen.sos << " pos=" << phen.pos << " eos=" << phen.eos );
        items.push_back( { "phenology", ok ? 25 : 0, 25 } );
    }

    // Item 4 (25): breakpoint localization on the harmonic pixel.
    {
        const auto series = cube->extractPixelSeries( 40, 40 );
        const auto breaks =
            BreakpointDetector::detectHarmonicBreaks( series, t, 2, 2, 10, 0.05 );
        const bool ok = breaks.valid && breaks.breakCount == 1 &&
                        breaks.breakpoints[0].index == 23 &&
                        std::abs( breaks.breakpoints[0].magnitude - ( -0.35 ) ) < 0.06 &&
                        breaks.breakpoints[0].pValue < 0.05;
        INFO( "item4 breaks=" << breaks.breakCount );
        items.push_back( { "breakpoint", ok ? 25 : 0, 25 } );
    }

    int total = 0;
    for ( const GradingItem &item : items )
    {
        INFO( item.name << ": " << item.score << "/" << item.max );
        REQUIRE( item.score == item.max );
        total += item.score;
    }
    REQUIRE( total == 100 );

    // Hardware redline for the whole grading flow.
    REQUIRE( peakRssBytes() < 1536ull * 1024 * 1024 );
}
