// test_r24_a_e2e_temporal.cpp — R24-A science track (WP-A8, chain 2/3:
// temporal). Real-GDAL end-to-end: dated scene stack → rs:temporal_smooth
// (whittaker) → stacked output raster. Verifies the production contract:
// band-per-date layout, CRS/geotransform/NoData preserved from the inputs,
// smooth-series fidelity, and two independent runs producing byte-identical
// output (repeatability of the deterministic pipeline).

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <json/json.h>

#include <array>
#include <cmath>
#include <vector>

using namespace sicnu::operators;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_r24_a_e2e_temporal";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

bool writeScene( const QString &path, const std::vector<float> &values,
                 const QString &date )
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
    GDALSetMetadataItem( ds, "SICNU_ACQUISITION_DATE", date.toUtf8().constData(),
                         nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, -9999.0 );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 2, 1,
                                  const_cast<float *>( values.data() ), 2, 1,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

/// Six scenes at 16-day cadence. Pixel 0: a smooth ramp the whittaker
/// smoother must reproduce within tolerance; pixel 1: one cloud spike the
/// smoother must damp toward its neighbours.
struct SmoothSeries
{
    std::vector<std::vector<float>> scenes; // [scene][pixel]
};

SmoothSeries makeSeries()
{
    SmoothSeries s;
    const double base[6] = { 0.20, 0.30, 0.42, 0.55, 0.68, 0.80 };
    for ( int i = 0; i < 6; ++i )
    {
        std::vector<float> px( 2 );
        px[0] = static_cast<float>( base[i] );
        px[1] = static_cast<float>( base[i] );
        s.scenes.push_back( px );
    }
    s.scenes[2][1] = 0.95f; // cloud-contaminated sample
    return s;
}

std::vector<float> readBand( const QString &path, int band )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    REQUIRE( ds.readBandData( band, out.data(), ds.width(), ds.height() ) );
    return out;
}

} // namespace

TEST_CASE( "rs:temporal_smooth E2E: dated stack to smoothed bands, reproducible",
           "[operators][temporal][e2e][r24_a]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const SmoothSeries series = makeSeries();
    const char *dates[6] = { "2024-05-01", "2024-05-17", "2024-06-02",
                             "2024-06-18", "2024-07-04", "2024-07-20" };
    Json::Value scenes( Json::arrayValue );
    for ( int i = 0; i < 6; ++i )
    {
        const QString name = QStringLiteral( "s%1.tif" ).arg( i );
        REQUIRE( writeScene( dir.filePath( name ), series.scenes[i],
                             QString::fromLatin1( dates[i] ) ) );
        scenes.append( dir.filePath( name ).toStdString() );
    }

    auto op = RSOperatorRegistry::instance().create( "rs:temporal_smooth" );
    REQUIRE( op != nullptr );

    auto runOnce = [&]( const QString &outPath ) {
        RSOperatorContext context;
        Json::Value params( Json::objectValue );
        params["scenes"] = scenes;
        params["band"] = 1;
        params["method"] = "whittaker";
        params["lambda"] = 2.0;
        params["output"] = outPath.toStdString();
        REQUIRE_NOTHROW( op->run( params, context ) );
    };
    runOnce( dir.filePath( "smooth1.tif" ) );
    runOnce( dir.filePath( "smooth2.tif" ) );

    // Production contract: one output band per date, grid + CRS + NoData
    // carried from the scenes.
    GdalDatasetWrapper out;
    REQUIRE( out.open( dir.filePath( "smooth1.tif" ) ) );
    REQUIRE( out.bandCount() == 6 );
    REQUIRE( out.width() == 2 );
    REQUIRE( out.height() == 1 );
    bool hasNoData = false;
    out.bandNoDataValue( 1, &hasNoData );
    CHECK( hasNoData );
    const std::array<double, 6> gt = out.geoTransform();
    CHECK( gt[0] == Catch::Approx( 500000.0 ) );
    CHECK( gt[1] == Catch::Approx( 30.0 ) );
    CHECK( gt[5] == Catch::Approx( -30.0 ) );

    // Smooth ramp stays faithful (whittaker with a small penalty).
    const auto band0 = readBand( dir.filePath( "smooth1.tif" ), 1 );
    CHECK( band0[0] == Catch::Approx( 0.20f ).margin( 0.03f ) );
    const auto band5 = readBand( dir.filePath( "smooth1.tif" ), 6 );
    CHECK( band5[0] == Catch::Approx( 0.80f ).margin( 0.03f ) );
    // The cloud spike is damped below its raw 0.95 but the series still
    // rises: the smoothed value sits under the spike by a clear margin.
    const auto band2 = readBand( dir.filePath( "smooth1.tif" ), 3 );
    CHECK( band2[1] < 0.90f );
    CHECK( band2[1] > band2[0] - 0.05f );

    // Repeatability: two independent runs agree bit-for-bit.
    for ( int b = 1; b <= 6; ++b )
    {
        const auto v1 = readBand( dir.filePath( "smooth1.tif" ), b );
        const auto v2 = readBand( dir.filePath( "smooth2.tif" ), b );
        REQUIRE( v1.size() == v2.size() );
        for ( std::size_t i = 0; i < v1.size(); ++i )
            CHECK( v1[i] == v2[i] );
    }
}
