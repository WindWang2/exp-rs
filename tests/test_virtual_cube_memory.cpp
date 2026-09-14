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
    REQUIRE( cube != nullptr );
    INFO( why.toStdString() );

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
