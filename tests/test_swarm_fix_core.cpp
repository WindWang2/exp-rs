/***************************************************************************
  tests/test_swarm_fix_core.cpp
  swarm-review 2026-11-04 — CORE-1..CORE-6 regression anchors.
  ---------------------------
  Each case pins one finding from SWARM_REVIEW.md against the first-party
  core modules. Fixtures are synthesized at runtime (repo convention: no
  committed geodata).

  Findings anchored here:
    CORE-1  TemporalCube::open compares the geotransform, not only size and
            CRS: same-size / same-CRS scenes on different grids are refused
            instead of being composited pixel-misregistered.
    CORE-2  A non-finite cloud fraction is excluded from compositing
            (std::max( 0.0, NaN ) used to clamp it to "clear").
    CORE-5  A world-writable plugin LIBRARY is refused, not only a
            world-writable plugin directory.
    CORE-6  Degenerate spatial-split configurations (NaN/inf block size,
            non-finite guard-ring / ratios, out-of-range block quotient)
            yield an empty report instead of undefined behaviour.

  Not anchored here (by design):
    CORE-3  unloadAll detaches the table entry before emitting
            pluginUnloaded. A behavioral anchor needs a real SICNU plugin
            library plus a slot that re-enters plugin(); the lifecycle
            contract is pinned by test_plugin_host.cpp
            ("... isPluginLoaded false after unloadAll").
    CORE-4  loadPlugin is idempotent for an already-loaded library. Same
            dependency: the guard runs before any plugin metadata exists,
            so a false-positive anchor cannot be written without a
            loadable SICNU plugin (test_plugin_host_allowlist covers the
            refusal side of the same channel).
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/plugin_host.h"
#include "core/spatial_split.h"
#include "core/temporal_cube.h"

#include <gdal_priv.h>
#include <cpl_conv.h>

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QLibraryInfo>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using Catch::Approx;
using rs::core::SampleRole;
using rs::core::SpatialBlockConfig;
using rs::core::SpatialBlockPartitioner;
using rs::core::SpatialSamplePoint;
using rs::core::SpatialSplitReport;
using sicnu::temporal::TemporalCube;
using sicnu::temporal::TemporalCubeConfig;

namespace
{

// GDAL driver registration is process-global; do it exactly once.
struct GdalInit
{
    GdalInit()
    {
        GDALAllRegister();
        GDALSetCacheMax64( 64 * 1024 * 1024 );
    }
};

const QDate kEpoch( 2020, 1, 1 );

float kNan = std::numeric_limits<float>::quiet_NaN();

std::vector<float> plane( int w, int h, float v )
{
    return std::vector<float>( static_cast<std::size_t>( w ) * h, v );
}

/// Writes a Float32 GTiff named `<stem>_<YYYYMMDD>.tif` (the filename is the
/// acquisition-instant source under test). @a geotransform defaults to the
/// north-up grid the other D16 suites use; pass a different one to build the
/// CORE-1 misregistration fixture. Returns the path.
QString writeScene( const QDir &dir, const QString &stem, const QDate &date, int w, int h,
                    const std::vector<float> &values,
                    const std::vector<float> &cloud = {},
                    const double *geotransform = nullptr )
{
    static const double kDefaultGt[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    const double *gt = geotransform ? geotransform : kDefaultGt;
    const QString path = dir.filePath(
        QStringLiteral( "%1_%2.tif" ).arg( stem, date.toString( QStringLiteral( "yyyyMMdd" ) ) ) );
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), w, h,
                                      cloud.empty() ? 1 : 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    ds->SetGeoTransform( const_cast<double *>( gt ) );
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

/// Same fixture, written WITHOUT a geotransform (a plain GTiff Create leaves
/// the placement unset) — the CORE-1 "unplaced scene" case.
QString writeSceneUnplaced( const QDir &dir, const QString &stem, const QDate &date, int w, int h,
                            const std::vector<float> &values )
{
    const QString path = dir.filePath(
        QStringLiteral( "%1_%2.tif" ).arg( stem, date.toString( QStringLiteral( "yyyyMMdd" ) ) ) );
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    const CPLErr err = ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, w, h,
                                                         const_cast<float *>( values.data() ), w, h,
                                                         GDT_Float32, 0, 0, nullptr );
    REQUIRE( err == CE_None );
    GDALClose( ds );
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// CORE-1: the grid contract is pixel ALIGNMENT. Equal dimensions and an equal
// CRS say nothing about registration: two scenes can share both and still be
// offset by a whole pixel (or rotated), and compositing them would silently
// misregister every measurement. The .h contract already promised refusal —
// this pins the behaviour.
// ---------------------------------------------------------------------------

TEST_CASE( "TemporalCube refuses scenes whose geotransform disagrees", "[swarm][core][cube]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QDir dir( tmp.path() );
    const int w = 8, h = 8;

    SECTION( "same size and same CRS, shifted origin is refused" )
    {
        // Scene B is registered one pixel to the east of scene A: identical
        // 8x8 grid, identical (absent) CRS, different placement.
        const double shifted[6] = { 1.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
        const std::vector<QString> scenes{
            writeScene( dir, "a", kEpoch, w, h, plane( w, h, 0.2f ) ),
            writeScene( dir, "b", kEpoch.addDays( 10 ), w, h, plane( w, h, 0.6f ),
                        {}, shifted ) };
        QString why;
        auto cube = TemporalCube::open( scenes, TemporalCubeConfig{}, &why );
        INFO( why.toStdString() );
        REQUIRE( cube == nullptr );
        CHECK( why.contains( QStringLiteral( "geotransform" ), Qt::CaseInsensitive ) );
    }

    SECTION( "a rotation/skew term disagrees even with the same origin" )
    {
        const double skewed[6] = { 0.0, 1.0, 0.01, 0.0, 0.0, -1.0 };
        const std::vector<QString> scenes{
            writeScene( dir, "c", kEpoch, w, h, plane( w, h, 0.2f ) ),
            writeScene( dir, "d", kEpoch.addDays( 10 ), w, h, plane( w, h, 0.6f ),
                        {}, skewed ) };
        QString why;
        auto cube = TemporalCube::open( scenes, TemporalCubeConfig{}, &why );
        INFO( why.toStdString() );
        REQUIRE( cube == nullptr );
    }

    SECTION( "identical geotransforms still composite (positive control)" )
    {
        // The whole existing D16 suite lives here: same grid, different days.
        const std::vector<QString> scenes{
            writeScene( dir, "e", kEpoch, w, h, plane( w, h, 0.2f ) ),
            writeScene( dir, "f", kEpoch.addDays( 10 ), w, h, plane( w, h, 0.6f ) ) };
        QString why;
        auto cube = TemporalCube::open( scenes, TemporalCubeConfig{}, &why );
        INFO( why.toStdString() );
        REQUIRE( cube != nullptr );
        const auto series = cube->extractPixelSeries( 0, 0 );
        REQUIRE( series.size() == 1 );
        REQUIRE( series[0] == Approx( 0.2 ).margin( 1e-6 ) ); // BestPixel: dt = 0 wins
    }

    SECTION( "a georeferenced scene never composites with an unplaced one" )
    {
        // Same size, no CRS on either side, but scene B declares no
        // geotransform at all: no pixel alignment can be established.
        const std::vector<QString> scenes{
            writeScene( dir, "g", kEpoch, w, h, plane( w, h, 0.2f ) ),
            writeSceneUnplaced( dir, "i", kEpoch.addDays( 10 ), w, h, plane( w, h, 0.6f ) ) };
        QString why;
        auto cube = TemporalCube::open( scenes, TemporalCubeConfig{}, &why );
        INFO( why.toStdString() );
        REQUIRE( cube == nullptr );
    }
}

// ---------------------------------------------------------------------------
// CORE-2: std::max( 0.0, NaN ) is 0.0, so a NaN cloud fraction used to clamp
// to "perfectly clear" and take the top compositing score — a masked pixel
// became the BestPixel winner. A pixel whose cloud band is not finite has no
// usable cloud semantics and is not a candidate at all.
// ---------------------------------------------------------------------------

TEST_CASE( "TemporalCube excludes pixels with a non-finite cloud fraction",
           "[swarm][core][cube]" )
{
    GdalInit init;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QDir dir( tmp.path() );
    const int w = 8, h = 8;

    // Two scenes 10 days apart -> a single calendar node (span 10 < 16).
    // Scene A (day 0, value 0.2) carries a NaN cloud band everywhere; scene B
    // (day 10, value 0.6) is clear. dt = 0 would make A the BestPixel winner
    // if its NaN cloud were clamped to 0.
    const std::vector<float> cloudNaN( static_cast<std::size_t>( w ) * h, kNan );
    const auto sceneA = writeScene( dir, "n", kEpoch, w, h, plane( w, h, 0.2f ), cloudNaN );
    const auto sceneB = writeScene( dir, "n", kEpoch.addDays( 10 ), w, h, plane( w, h, 0.6f ),
                                    plane( w, h, 0.0f ) );

    SECTION( "BestPixel skips the NaN-clouded observation" )
    {
        auto cube = TemporalCube::open( { sceneA, sceneB }, TemporalCubeConfig{} );
        REQUIRE( cube != nullptr );
        const auto series = cube->extractPixelSeries( 0, 0 );
        REQUIRE( series.size() == 1 );
        REQUIRE( series[0] == Approx( 0.6 ).margin( 1e-6 ) );
    }

    SECTION( "WeightedMean ignores the NaN-clouded weight too" )
    {
        TemporalCubeConfig cfg;
        cfg.strategy = sicnu::temporal::CompositingStrategy::WeightedMean;
        auto cube = TemporalCube::open( { sceneA, sceneB }, cfg );
        REQUIRE( cube != nullptr );
        const auto series = cube->extractPixelSeries( 0, 0 );
        REQUIRE( series.size() == 1 );
        REQUIRE( series[0] == Approx( 0.6 ).margin( 1e-6 ) );
    }

    SECTION( "control: a finite clear cloud band keeps the near observation" )
    {
        const auto clearA = writeScene( dir, "p", kEpoch, w, h, plane( w, h, 0.2f ),
                                        plane( w, h, 0.0f ) );
        const auto clearB = writeScene( dir, "p", kEpoch.addDays( 10 ), w, h,
                                        plane( w, h, 0.6f ), plane( w, h, 0.0f ) );
        auto cube = TemporalCube::open( { clearA, clearB }, TemporalCubeConfig{} );
        REQUIRE( cube != nullptr );
        const auto series = cube->extractPixelSeries( 0, 0 );
        REQUIRE( series.size() == 1 );
        REQUIRE( series[0] == Approx( 0.2 ).margin( 1e-6 ) );
    }
}

// ---------------------------------------------------------------------------
// CORE-5: the world-writable trust check covered the plugin directory only.
// A library anyone can write is the same break inside a locked directory, so
// the file itself is tested before it is dlopen'ed.
// ---------------------------------------------------------------------------

TEST_CASE( "PluginHost refuses a world-writable plugin library", "[swarm][core][plugin_host]" )
{
    // Stand-in for a dropped-in library: any real Qt plugin that is not a
    // SicnuPluginInterface (SQL driver / image format / platform theme). The
    // refusal fires before instance(), so the library never runs code.
    const QString root = QLibraryInfo::path( QLibraryInfo::PluginsPath );
    QString foreign;
    for ( const QString &sub : { QStringLiteral( "sqldrivers" ), QStringLiteral( "imageformats" ),
                                 QStringLiteral( "platforms" ) } )
    {
        const QDir dir( QDir( root ).filePath( sub ) );
        for ( const QString &name : dir.entryList( QDir::Files ) )
        {
            const QString path = dir.absoluteFilePath( name );
            if ( QLibrary::isLibrary( path ) )
            {
                foreign = path;
                break;
            }
        }
        if ( !foreign.isEmpty() )
            break;
    }
    if ( foreign.isEmpty() )
        SKIP( "no Qt plugin available to stand in for a dropped-in library" );

    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString suffix = QFileInfo( foreign ).fileName().section( QLatin1Char( '.' ), 1 );
    const QString dropped = QDir( tmp.path() ).filePath( QStringLiteral( "libsealed_dropin." )
                                                         + suffix );
    REQUIRE( QFile::copy( foreign, dropped ) );
    const QFileDevice::Permissions sealed = QFileInfo( dropped ).permissions();
    REQUIRE( QFile::setPermissions( dropped, sealed | QFileDevice::WriteOther ) );

    PluginHost host;
    host.setPythonPluginsEnabled( false );
    host.setTrustedNativePlugins( { QStringLiteral( "sealed_dropin" ) } );
    QStringList errors;
    QObject::connect( &host, &PluginHost::pluginError,
                      [&errors]( const QString &, const QString &error ) { errors << error; } );

    host.loadPlugins( tmp.path() );

    QFile::setPermissions( dropped, sealed ); // leave the fixture clean
    CHECK( host.loadedPlugins().isEmpty() );
    REQUIRE( errors.size() == 1 );
    CHECK( errors.first().contains( QStringLiteral( "world-writable" ) ) );
}

// ---------------------------------------------------------------------------
// CORE-6: degenerate split configurations. `NaN <= 0.0` is false, so a NaN or
// infinite block size used to reach the int64 cast (undefined behaviour for a
// non-finite or out-of-range quotient); a non-finite guard ring silently
// switched the leakage demotion off. All of them now refuse with an empty
// report, and a usable configuration still partitions.
// ---------------------------------------------------------------------------

namespace
{

// 3x3 unit-spaced lattice: coordinates 0..2 in both axes.
std::vector<SpatialSamplePoint> lattice3x3()
{
    std::vector<SpatialSamplePoint> pts;
    for ( int y = 0; y < 3; ++y )
        for ( int x = 0; x < 3; ++x )
        {
            SpatialSamplePoint p;
            p.id = static_cast<std::int64_t>( pts.size() );
            p.x = static_cast<double>( x );
            p.y = static_cast<double>( y );
            p.label = ( x + y ) % 2;
            pts.push_back( p );
        }
    return pts;
}

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

} // namespace

TEST_CASE( "Spatial split refuses degenerate configurations with an empty report",
           "[swarm][core][spatial]" )
{
    const std::vector<SpatialSamplePoint> samples = lattice3x3();

    SECTION( "NaN block width" )
    {
        SpatialBlockConfig cfg;
        cfg.blockWidth = kNaN;
        CHECK( SpatialBlockPartitioner().partition( samples, cfg ).assignments.empty() );
    }

    SECTION( "infinite block height" )
    {
        SpatialBlockConfig cfg;
        cfg.blockHeight = kInf;
        CHECK( SpatialBlockPartitioner().partition( samples, cfg ).assignments.empty() );
    }

    SECTION( "NaN guard ring would silently disable the leakage buffer" )
    {
        SpatialBlockConfig cfg;
        cfg.bufferDistance = kNaN;
        CHECK( SpatialBlockPartitioner().partition( samples, cfg ).assignments.empty() );
    }

    SECTION( "NaN train ratio would collapse every block into one fold" )
    {
        SpatialBlockConfig cfg;
        cfg.trainRatio = kNaN;
        CHECK( SpatialBlockPartitioner().partition( samples, cfg ).assignments.empty() );
    }

    SECTION( "an out-of-range block quotient is refused, not cast" )
    {
        // Block width far below the coordinate span: the quotient leaves the
        // int64 range, so the cast itself would be undefined.
        SpatialBlockConfig cfg;
        cfg.blockWidth = std::numeric_limits<double>::denorm_min();
        cfg.blockHeight = std::numeric_limits<double>::denorm_min();
        CHECK( SpatialBlockPartitioner().partition( samples, cfg ).assignments.empty() );
    }

    SECTION( "a usable configuration still partitions every sample" )
    {
        SpatialBlockConfig cfg;
        cfg.blockWidth = 1.0; // every lattice column is its own block
        cfg.blockHeight = 1.0;
        cfg.bufferDistance = 0.5;
        const SpatialSplitReport report = SpatialBlockPartitioner().partition( samples, cfg );
        REQUIRE( report.assignments.size() == samples.size() );
        CHECK( report.trainCount + report.valCount + report.testCount + report.bufferCount
               == samples.size() );
        for ( std::size_t i = 0; i < samples.size(); ++i )
        {
            CHECK( report.assignments[i] != SampleRole::Unassigned );
            if ( report.assignments[i] == SampleRole::Train )
                continue;
            // The header invariant, checked against the code's own output:
            // an evaluation sample keeps its role only strictly outside the
            // guard ring, and every demoted sample lies inside it.
            for ( std::size_t j = 0; j < samples.size(); ++j )
            {
                if ( report.assignments[j] != SampleRole::Train )
                    continue;
                const double dx = samples[i].x - samples[j].x;
                const double dy = samples[i].y - samples[j].y;
                const double distance = std::sqrt( dx * dx + dy * dy );
                if ( report.assignments[i] == SampleRole::ExcludedBuffer )
                    CHECK( distance <= cfg.bufferDistance );
                else
                    CHECK( distance > cfg.bufferDistance );
            }
        }
    }
}
