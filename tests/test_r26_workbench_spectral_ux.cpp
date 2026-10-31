// test_r26_workbench_spectral_ux.cpp — R26-02 workbench: spectral profile
// honesty about NoData sentinels (RED driver: a masked pixel must cross the
// widget as NaN, never as a real -9999/0 sample).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsrasterlayer.h>

#include <gdal.h>

#include "app/widgets/spectral_profile_widget.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using Catch::Approx;

namespace
{

struct SpectralFixture
{
    SpectralFixture()
    {
        if ( !QCoreApplication::instance() )
        {
            static int argc = 1;
            static char appName[] = "test_r26_workbench_spectral_ux";
            static char *argv[] = { appName, nullptr };
            s_app = new QApplication( argc, argv );
            QgsApplication::initQgis();
        }
    }
    static QApplication *s_app;
};
QApplication *SpectralFixture::s_app = nullptr;

/// 4x4, 2 bands, geotransform (0,1,0,0,-1): pixel (1,1) is at map (1.5,-1.5).
/// Band values row*10+col / 100+row*10+col; pixel (1,1) of BOTH bands is the
/// NoData sentinel -999.
QString makeNoDataRaster( const QString &path )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::vector<std::vector<float>> bands( 2, std::vector<float>( 16, 0.0f ) );
    for ( int row = 0; row < 4; ++row )
    {
        for ( int col = 0; col < 4; ++col )
        {
            bands[0][static_cast<size_t>( row * 4 + col )] = static_cast<float>( row * 10 + col );
            bands[1][static_cast<size_t>( row * 4 + col )] = 100.0f + static_cast<float>( row * 10 + col );
        }
    }
    bands[0][5] = -999.0f;  // pixel (row 1, col 1)
    bands[1][5] = -999.0f;

    QString err;
    if ( !writeGdalOutput( path, 4, 4, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) )
        return err;
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    if ( !ds )
        return QStringLiteral( "open for NoData update failed" );
    GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), -999.0 );
    GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 2 ), -999.0 );
    GDALClose( ds );
    return {};
}

}  // namespace

TEST_CASE( "SpectralProfileWidget turns NoData sentinels into NaN, not plotted values",
           "[app][widgets][spectral][r26-workbench]" )
{
    SpectralFixture fixture;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString rasterPath = tmp.filePath( QStringLiteral( "nodata_profile.tif" ) );
    REQUIRE( makeNoDataRaster( rasterPath ).isEmpty() );

    auto layer = std::make_unique<QgsRasterLayer>( rasterPath, QStringLiteral( "nodata_raster" ) );
    REQUIRE( layer->isValid() );

    SpectralProfileWidget widget;

    SECTION( "clicking a NoData pixel yields NaN values, not the sentinel" )
    {
        widget.setProfile( QgsPointXY( 1.5, -1.5 ), layer.get() );
        REQUIRE( widget.values().size() == 2 );
        CHECK( std::isnan( widget.values()[0] ) );
        CHECK( std::isnan( widget.values()[1] ) );
    }

    SECTION( "a valid pixel next to NoData keeps real values and range" )
    {
        widget.setProfile( QgsPointXY( 2.5, -1.5 ), layer.get() );
        REQUIRE( widget.values().size() == 2 );
        REQUIRE_THAT( widget.values()[0], Catch::Matchers::WithinAbs( 12.0, 1e-3 ) );
        REQUIRE_THAT( widget.values()[1], Catch::Matchers::WithinAbs( 112.0, 1e-3 ) );
    }
}
