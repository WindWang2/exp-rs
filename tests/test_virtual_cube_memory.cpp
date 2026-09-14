// tests/test_virtual_cube_memory.cpp — D16 Package A: regularized temporal
// virtual cube (out-of-core). Numeric references against independent
// oracles: QDate calendar arithmetic and hand-placed composite/hole geometry.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/temporal_cube.h"

#include <gdal_priv.h>
#include <cpl_conv.h>

#include <QDate>
#include <QDir>
#include <QTemporaryDir>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using Catch::Approx;
using sicnu::temporal::CalendarPoint;
using sicnu::temporal::TemporalCube;
using sicnu::temporal::TemporalCubeConfig;

namespace
{

// GDAL driver registration is process-global; do it exactly once.
struct GdalInit
{
    GdalInit() { GDALAllRegister(); }
};

QDate kEpoch( 2020, 1, 1 );

float kNan = std::numeric_limits<float>::quiet_NaN();

std::vector<float> plane( int w, int h, float v )
{
    return std::vector<float>( static_cast<std::size_t>( w ) * h, v );
}

/// Writes a single-band (or band-2 cloud) Float32 GTiff named
/// `<stem>_<YYYYMMDD>.tif` — the filename is the acquisition-instant source
/// under test. Returns the path.
QString writeScene( const QDir &dir, const QString &stem, const QDate &date,
                    int w, int h, const std::vector<float> &values,
                    const std::vector<float> &cloud = {} )
{
    const QString path = dir.filePath(
        QStringLiteral( "%1_%2.tif" ).arg( stem, date.toString( QStringLiteral( "yyyyMMdd" ) ) ) );
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), w, h,
                                      cloud.empty() ? 1 : 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    ds->SetGeoTransform( gt );
    CPLErr err = ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, w, h,
                                                   const_cast<float *>( values.data() ), w, h,
                                                   GDT_Float32, 0, 0, nullptr );
    REQUIRE( err == CE_None );
    if ( !cloud.empty() )
    {
        err = ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, 0, w, h,
                                                const_cast<float *>( cloud.data() ), w, h,
                                                GDT_Float32, 0, 0, nullptr );
        REQUIRE( err == CE_None );
    }
    GDALClose( ds );
    return path;
}

} // namespace

TEST_CASE( "Sixteen-day regularized calendar spans the scene set exactly",
           "[d16][cube]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QDir dir( tmp.path() );

    const int w = 10, h = 10;
    const std::vector<QString> scenes{
        writeScene( dir, "s", kEpoch, w, h, plane( w, h, 0.2f ) ),
        writeScene( dir, "s", QDate( 2020, 6, 15 ), w, h, plane( w, h, 0.5f ) ),
        writeScene( dir, "s", QDate( 2021, 12, 31 ), w, h, plane( w, h, 0.1f ) ) };

    QString why;
    auto cube = TemporalCube::open( scenes, TemporalCubeConfig{}, &why );
    INFO( why.toStdString() );
    REQUIRE( cube != nullptr );

    // Scene span: 2020-01-01 (day 0) .. 2021-12-31 (day 730). Grid nodes
    // t_k = 16k with 0 <= 16k <= 730 -> k = 0..45 -> 46 slices (D16 spec §A).
    REQUIRE( cube->width() == w );
    REQUIRE( cube->height() == h );
    REQUIRE( cube->sliceCount() == 46 );

    const std::vector<CalendarPoint> tl = cube->timeline();
    REQUIRE( tl.size() == 46 );

    // Every node: exact 16-day offset and the QDate-oracle ISO date.
    // Hand-computed anchors: day 0 -> 2020-01-01, day 720 -> 2021-12-21.
    REQUIRE( tl.front().tDays == Approx( 0.0 ) );
    REQUIRE( tl.front().isoDate == QStringLiteral( "2020-01-01" ) );
    REQUIRE( tl.back().tDays == Approx( 720.0 ) );
    REQUIRE( tl.back().isoDate == QStringLiteral( "2021-12-21" ) );
    for ( std::size_t i = 0; i < tl.size(); ++i )
    {
        REQUIRE( tl[i].tDays == Approx( 16.0 * static_cast<double>( i ) ) );
        REQUIRE( tl[i].isoDate == kEpoch.addDays( static_cast<int>( 16 * i ) )
                                      .toString( QStringLiteral( "yyyy-MM-dd" ) ) );
    }
}

TEST_CASE( "Calendar snaps the first node up to the grid when scenes start off-grid",
           "[d16][cube]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    const QDir dir( tmp.path() );
    const int w = 8, h = 8;

    // 2020-01-05 is day 4 -> first node k = ceil(4/16) = 1 (day 16).
    const std::vector<QString> scenes{
        writeScene( dir, "s", QDate( 2020, 1, 5 ), w, h, plane( w, h, 0.3f ) ),
        writeScene( dir, "s", QDate( 2021, 6, 1 ), w, h, plane( w, h, 0.4f ) ) };

    auto cube = TemporalCube::open( scenes, TemporalCubeConfig{} );
    REQUIRE( cube != nullptr );
    const auto tl = cube->timeline();
    REQUIRE( !tl.empty() );
    REQUIRE( tl.front().tDays == Approx( 16.0 ) );
    REQUIRE( tl.front().isoDate == QStringLiteral( "2020-01-17" ) );
    // Last node: 2021-06-01 is day 517; floor(517/16) = 32 -> day 512.
    REQUIRE( tl.back().tDays == Approx( 512.0 ) );
    REQUIRE( cube->sliceCount() == 32 ); // k = 1..32
}

TEST_CASE( "Custom cadence scales the grid arithmetic", "[d16][cube]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    const QDir dir( tmp.path() );
    const int w = 8, h = 8;

    const std::vector<QString> scenes{
        writeScene( dir, "s", kEpoch, w, h, plane( w, h, 0.2f ) ),
        writeScene( dir, "s", QDate( 2021, 12, 31 ), w, h, plane( w, h, 0.3f ) ) };

    TemporalCubeConfig cfg;
    cfg.cadenceDays = 8;
    auto cube = TemporalCube::open( scenes, cfg );
    REQUIRE( cube != nullptr );
    // floor(730/8) = 91 -> 92 nodes at k = 0..91.
    REQUIRE( cube->sliceCount() == 92 );
    const auto tl = cube->timeline();
    REQUIRE( tl[7].tDays == Approx( 56.0 ) );
    REQUIRE( tl[7].isoDate == QStringLiteral( "2020-02-26" ) );
}

TEST_CASE( "Open refuses scenes with unparseable dates, mismatched grids, or an empty list",
           "[d16][cube]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    const QDir dir( tmp.path() );
    const int w = 8, h = 8;

    SECTION( "empty scene list" )
    {
        QString why;
        auto cube = TemporalCube::open( {}, TemporalCubeConfig{}, &why );
        REQUIRE( cube == nullptr );
        REQUIRE( !why.isEmpty() );
    }

    SECTION( "filename without any ISO date" )
    {
        const QString path = dir.filePath( QStringLiteral( "undated_scene.tif" ) );
        GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
        GDALDataset *ds = driver->Create( path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr );
        REQUIRE( ds != nullptr );
        GDALClose( ds );

        QString why;
        auto cube = TemporalCube::open( { path }, TemporalCubeConfig{}, &why );
        REQUIRE( cube == nullptr );
        REQUIRE( !why.isEmpty() );
    }

    SECTION( "mismatched raster size" )
    {
        const std::vector<QString> scenes{
            writeScene( dir, "s", QDate( 2020, 1, 1 ), w, h, plane( w, h, 0.2f ) ),
            writeScene( dir, "s", QDate( 2020, 2, 1 ), w + 3, h, plane( w + 3, h, 0.2f ) ) };
        QString why;
        auto cube = TemporalCube::open( scenes, TemporalCubeConfig{}, &why );
        REQUIRE( cube == nullptr );
        REQUIRE( !why.isEmpty() );
    }
}

// ---------------------------------------------------------------------------
// Slice 2: out-of-core tile engine — bounded-memory chunk reads on a
// 50-scene 2048×2048 stack. Fixtures live under the build dir (disk-backed;
// /tmp is tmpfs on this host) and are removed after the case.
// ---------------------------------------------------------------------------

#include <sys/resource.h>

namespace
{
/// Process peak RSS in bytes (Linux ru_maxrss is KiB).
std::size_t peakRssBytes()
{
    rusage usage;
    getrusage( RUSAGE_SELF, &usage );
    return static_cast<std::size_t>( usage.ru_maxrss ) * 1024ull;
}

/// Disk-backed fixture directory under the build tree, removed on scope exit.
class BuildDirFixtureDir
{
  public:
    BuildDirFixtureDir()
    {
        const QString root = QStringLiteral( CMAKE_BINARY_DIR ) + QStringLiteral( "/d16_fixtures" );
        QDir().mkpath( root );
        const QString unique = root + QStringLiteral( "/%1_%2" )
                                       .arg( QCoreApplicationName() )
                                       .arg( reinterpret_cast<quintptr>( this ), 0, 16 );
        QDir().mkpath( unique );
        mDir = unique;
    }
    ~BuildDirFixtureDir() { QDir( mDir ).removeRecursively(); }
    QDir dir() const { return QDir( mDir ); }

  private:
    static QString QCoreApplicationName() { return QStringLiteral( "d16" ); }
    QString mDir;
};
} // namespace

TEST_CASE( "Out-of-core chunk reads stay under the 1.5 GB peak-RSS redline",
           "[d16][cube][memory]" )
{
    GdalInit init;
    BuildDirFixtureDir fixture;
    const QDir dir = fixture.dir();
    const int w = 2048, h = 2048;

    // 50 scenes, deterministic non-uniform spacing {6,11,16,21} days from
    // the epoch: dates {54c, 54c+6, 54c+17, 54c+33} per cycle c, last scene
    // at day 654 -> floor(654/16) = 40 -> 41 calendar slices.
    constexpr int kSceneCount = 50;
    std::vector<QDate> dates;
    dates.reserve( kSceneCount );
    QDate cursor = kEpoch;
    for ( int i = 0; i < kSceneCount; ++i )
    {
        dates.push_back( cursor );
        cursor = cursor.addDays( 6 + 5 * ( i % 4 ) );
    }

    std::vector<QString> scenes;
    std::vector<double> dayOffsets; // independent oracle copy of the instants
    for ( int i = 0; i < kSceneCount; ++i )
    {
        // Slow seasonal ramp, constant across space: compositing truth at a
        // pixel is the value of the winning (nearest) acquisition.
        const double t = static_cast<double>( dates[i].toJulianDay() - kEpoch.toJulianDay() );
        dayOffsets.push_back( t );
        scenes.push_back(
            writeScene( dir, "s", dates[i], w, h,
                        plane( w, h, static_cast<float>( 0.2 + 0.3 * std::sin( 2.0 * M_PI * t / 365.0 ) ) ) ) );
    }

    QString why;
    auto cube = TemporalCube::open( scenes, TemporalCubeConfig{}, &why );
    INFO( why.toStdString() );
    REQUIRE( cube != nullptr );

    // Day 654 -> kMax = 40 -> 41 nodes.
    REQUIRE( cube->sliceCount() == 41 );
    REQUIRE( cube->width() == w );
    REQUIRE( cube->height() == h );

    // Full-tile-range chunk: time-major layout [41 x 512 x 512].
    const auto chunk = cube->readChunk( 0, 0, 512, 512, 0, cube->sliceCount() );
    REQUIRE( chunk.size() == static_cast<std::size_t>( 512 ) * 512 * cube->sliceCount() );

    // Densely sampled stack (max spacing 23 < 45) -> zero gap nodes: every
    // composed value equals its window's nearest acquisition (BestPixel,
    // lowest scene index on exact ties — the documented input-order rule).
    const auto tl = cube->timeline();
    std::size_t nanCount = 0;
    for ( int k = 0; k < cube->sliceCount(); ++k )
    {
        std::size_t best = 0;
        double bestAbs = std::numeric_limits<double>::infinity();
        for ( std::size_t s = 0; s < dayOffsets.size(); ++s )
        {
            const double d = std::abs( dayOffsets[s] - tl[k].tDays );
            if ( d < bestAbs )
            {
                bestAbs = d;
                best = s;
            }
        }
        const float expected =
            static_cast<float>( 0.2 + 0.3 * std::sin( 2.0 * M_PI * dayOffsets[best] / 365.0 ) );
        const float got = chunk[static_cast<std::size_t>( k ) * 512 * 512 + 257ull * 512 + 257];
        if ( std::isnan( got ) )
            ++nanCount;
        else
            REQUIRE( got == Approx( expected ).margin( 1e-6 ) );
    }
    REQUIRE( nanCount == 0 );

    // Pixel series goes through the same tiles and must agree exactly.
    const auto series = cube->extractPixelSeries( 100, 200 );
    REQUIRE( series.size() == static_cast<std::size_t>( cube->sliceCount() ) );
    for ( int k = 0; k < cube->sliceCount(); ++k )
        REQUIRE( series[k] == chunk[static_cast<std::size_t>( k ) * 512 * 512 + 200ull * 512 + 100] );

    // Hardware redline (D16 §A): whole-flow peak RSS strictly below 1.5 GB.
    const std::size_t peak = peakRssBytes();
    INFO( "peak RSS bytes: " << peak );
    REQUIRE( peak < 1536ull * 1024 * 1024 );
}

TEST_CASE( "readChunk rejects invalid geometry with an empty result", "[d16][cube]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    const QDir dir( tmp.path() );
    const std::vector<QString> scenes{
        writeScene( dir, "s", kEpoch, 16, 16, plane( 16, 16, 0.3f ) ),
        writeScene( dir, "s", QDate( 2020, 3, 1 ), 16, 16, plane( 16, 16, 0.4f ) ) };

    auto cube = TemporalCube::open( scenes, TemporalCubeConfig{} );
    REQUIRE( cube != nullptr );

    SECTION( "out-of-range spatial window" ) { REQUIRE( cube->readChunk( 15, 0, 4, 4, 0, 1 ).empty() ); }
    SECTION( "degenerate window" ) { REQUIRE( cube->readChunk( 0, 0, 0, 4, 0, 1 ).empty() ); }
    SECTION( "time window beyond the calendar" )
    {
        REQUIRE( cube->readChunk( 0, 0, 4, 4, cube->sliceCount(), 1 ).empty() );
    }
    SECTION( "extractPixelSeries out of range" )
    {
        REQUIRE( cube->extractPixelSeries( -1, 0 ).empty() );
        REQUIRE( cube->extractPixelSeries( 0, 16 ).empty() );
    }
}

// ---------------------------------------------------------------------------
// Slice 3: compositing policy contracts — cloud masking, WeightedMean,
// 45-day gap guard, NoData exclusion.
// ---------------------------------------------------------------------------

namespace
{
/// Variant of writeScene with an explicit nodata declaration.
QString writeSceneNoData( const QDir &dir, const QDate &date, int w, int h,
                          const std::vector<float> &values, float noDataValue )
{
    const QString path = dir.filePath(
        QStringLiteral( "nd_%1.tif" ).arg( date.toString( QStringLiteral( "yyyyMMdd" ) ) ) );
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    ds->GetRasterBand( 1 )->SetNoDataValue( noDataValue );
    CPLErr err = ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, w, h,
                                                   const_cast<float *>( values.data() ), w, h,
                                                   GDT_Float32, 0, 0, nullptr );
    REQUIRE( err == CE_None );
    GDALClose( ds );
    return path;
}
} // namespace

TEST_CASE( "Cloud mask and WeightedMean policy shape the composite", "[d16][cube][composite]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    const QDir dir( tmp.path() );
    const int w = 8, h = 8;

    // Two scenes 10 days apart -> a single calendar node (span 10 < 16).
    // Scene A (day 0, value 0.2): clear except one fully-clouded pixel.
    // Scene B (day 10, value 0.6): half-clouded everywhere.
    std::vector<float> a = plane( w, h, 0.2f );
    std::vector<float> cloudA( static_cast<std::size_t>( w ) * h, 0.0f );
    const int opaqueIdx = 3 * w + 5;
    cloudA[opaqueIdx] = 1.0f;
    std::vector<float> b = plane( w, h, 0.6f );
    std::vector<float> cloudB = plane( w, h, 0.5f );
    const auto sceneA = writeScene( dir, "c", kEpoch, w, h, a, cloudA );
    const auto sceneB = writeScene( dir, "c", kEpoch.addDays( 10 ), w, h, b, cloudB );

    // sigma = W/2 = 16; node day 0: gauss_A = 1, gauss_B = exp(-100/512).
    const double gaussB = std::exp( -100.0 / ( 2.0 * 16.0 * 16.0 ) );
    const double expectedMean = ( 1.0 * 0.2 + 0.5 * gaussB * 0.6 ) / ( 1.0 + 0.5 * gaussB );

    SECTION( "WeightedMean blends by (1-cloud)*gauss weights" )
    {
        TemporalCubeConfig cfg;
        cfg.strategy = sicnu::temporal::CompositingStrategy::WeightedMean;
        auto cube = TemporalCube::open( { sceneA, sceneB }, cfg );
        REQUIRE( cube != nullptr );
        const auto series = cube->extractPixelSeries( 0, 0 );
        REQUIRE( series.size() == 1 );
        REQUIRE( series[0] == Approx( expectedMean ).margin( 1e-6 ) );
        // The opaque A pixel drops out; B alone defines the weighted mean.
        const auto opaque = cube->extractPixelSeries( 5, 3 );
        REQUIRE( opaque[0] == Approx( 0.6 ).margin( 1e-6 ) );
    }

    SECTION( "BestPixel skips fully clouded candidates" )
    {
        auto cube = TemporalCube::open( { sceneA, sceneB }, TemporalCubeConfig{} );
        REQUIRE( cube != nullptr );
        // Clear pixel: Q_A = 1 beats Q_B = 0.5*gaussB.
        const auto series = cube->extractPixelSeries( 0, 0 );
        REQUIRE( series.size() == 1 );
        REQUIRE( series[0] == Approx( 0.2 ).margin( 1e-6 ) );
        // Opaque A pixel: Q_A = 0, the candidate is excluded, B wins.
        const auto opaque = cube->extractPixelSeries( 5, 3 );
        REQUIRE( opaque[0] == Approx( 0.6 ).margin( 1e-6 ) );
    }
}

TEST_CASE( "Holes wider than 45 days yield NaN, narrower holes stay composed",
           "[d16][cube][gap]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    const QDir dir( tmp.path() );
    const int w = 8, h = 8;

    SECTION( "50-day hole: interior nodes are NaN, node at the scene stays" )
    {
        const std::vector<QString> scenes{
            writeScene( dir, "g", kEpoch, w, h, plane( w, h, 0.3f ) ),
            writeScene( dir, "g", kEpoch.addDays( 50 ), w, h, plane( w, h, 0.7f ) ) };
        auto cube = TemporalCube::open( scenes, TemporalCubeConfig{} );
        REQUIRE( cube != nullptr );
        // Nodes: 0, 16, 32, 48 (kMax = floor(50/16) = 3).
        const auto series = cube->extractPixelSeries( 0, 0 );
        REQUIRE( series.size() == 4 );
        REQUIRE( series[0] == Approx( 0.3 ).margin( 1e-6 ) );
        REQUIRE( std::isnan( series[1] ) );
        REQUIRE( std::isnan( series[2] ) );
        REQUIRE( std::isnan( series[3] ) );
    }

    SECTION( "40-day hole: bracketing span 40 <= 45 composes" )
    {
        const std::vector<QString> scenes{
            writeScene( dir, "g", kEpoch, w, h, plane( w, h, 0.3f ) ),
            writeScene( dir, "g", kEpoch.addDays( 40 ), w, h, plane( w, h, 0.7f ) ) };
        auto cube = TemporalCube::open( scenes, TemporalCubeConfig{} );
        REQUIRE( cube != nullptr );
        const auto series = cube->extractPixelSeries( 0, 0 );
        REQUIRE( series.size() == 3 ); // nodes 0, 16, 32
        REQUIRE( !std::isnan( series[1] ) );
        REQUIRE( !std::isnan( series[2] ) );
    }
}

TEST_CASE( "Declared NoData pixels are never compositing candidates",
           "[d16][cube][nodata]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    const QDir dir( tmp.path() );
    const int w = 8, h = 8;

    std::vector<float> v = plane( w, h, 0.4f );
    v[2 * w + 2] = -9999.0f;
    const auto scene = writeSceneNoData( dir, kEpoch, w, h, v, -9999.0f );

    auto cube = TemporalCube::open( { scene }, TemporalCubeConfig{} );
    REQUIRE( cube != nullptr );
    const auto series = cube->extractPixelSeries( 2, 2 );
    REQUIRE( series.size() == 1 ); // single scene at day 0 -> single node
    REQUIRE( std::isnan( series[0] ) );
    const auto ok = cube->extractPixelSeries( 0, 0 );
    REQUIRE( ok[0] == Approx( 0.4 ).margin( 1e-6 ) );
}
