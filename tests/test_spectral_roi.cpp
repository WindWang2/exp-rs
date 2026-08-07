// test_spectral_roi.cpp — ROI mean spectrum extraction
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <gdal.h>

#include <QPolygonF>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <vector>

#include "processing/algorithms/spectral_roi.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;

namespace {

/// 4x2 raster, 2 bands, GT {0,1,0,0,0,-1} (map y = -row). Band 1 value =
/// row*10 + col; band 2 = 100 + value. Optionally stamps WAVELENGTH.
QString makeRoiRaster( const QString &path, bool withWavelengths = false )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::vector<std::vector<float>> bands( 2, std::vector<float>( 8, 0.0f ) );
    for ( int row = 0; row < 2; ++row )
    {
        for ( int col = 0; col < 4; ++col )
        {
            bands[0][static_cast<size_t>( row * 4 + col )] = static_cast<float>( row * 10 + col );
            bands[1][static_cast<size_t>( row * 4 + col )] = 100.0f + static_cast<float>( row * 10 + col );
        }
    }
    QString err;
    REQUIRE( writeGdalOutput( path, 4, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
    if ( withWavelengths )
    {
        GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
        REQUIRE( ds != nullptr );
        GDALSetMetadataItem( GDALGetRasterBand( ds, 1 ), "WAVELENGTH", "490", nullptr );
        GDALSetMetadataItem( GDALGetRasterBand( ds, 2 ), "WAVELENGTH", "665", nullptr );
        GDALClose( ds );
    }
    return {};
}

} // namespace

TEST_CASE( "ROI mean spectrum averages the covered pixels", "[spectral_roi]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString path = tmp.filePath( QStringLiteral( "roi.tif" ) );
    REQUIRE( makeRoiRaster( path ).isEmpty() );

    // ROI = left half (map x in [0, 2)): covers pixels col 0,1 in both rows.
    QPolygonF polygon;
    polygon << QPointF( 0.0, 0.0 ) << QPointF( 2.0, 0.0 )
            << QPointF( 2.0, -2.0 ) << QPointF( 0.0, -2.0 );

    SpectralRoiProfile::RoiProfileResult result;
    QString err;
    REQUIRE( SpectralRoiProfile::meanSpectrum( path, polygon, &result, &err ) );

    // Band 1 covered values: row0 col0..1 = {0,1}, row1 col0..1 = {10,11} -> mean 5.5
    CHECK( result.pixelCount == 4 );
    REQUIRE( result.mean.size() == 2 );
    CHECK( result.mean[0] == Approx( 5.5f ).margin( 1e-4f ) );
    // Band 2 = 100 + band1 -> mean 105.5
    CHECK( result.mean[1] == Approx( 105.5f ).margin( 1e-4f ) );
    // Stddev of {0,1,10,11}: sqrt(mean(x^2) - mean(x)^2) = sqrt(55.5 - 30.25)
    CHECK( result.stddev[0] == Approx( 5.0249f ).margin( 1e-4f ) );
}

TEST_CASE( "ROI mean spectrum exposes wavelengths and guards inputs", "[spectral_roi]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString path = tmp.filePath( QStringLiteral( "roi2.tif" ) );
    REQUIRE( makeRoiRaster( path, /*withWavelengths=*/true ).isEmpty() );

    SECTION( "Wavelength grid from band metadata" )
    {
        QPolygonF polygon;
        polygon << QPointF( 0.0, 0.0 ) << QPointF( 4.0, 0.0 )
                << QPointF( 4.0, -2.0 ) << QPointF( 0.0, -2.0 );
        SpectralRoiProfile::RoiProfileResult result;
        QString err;
        REQUIRE( SpectralRoiProfile::meanSpectrum( path, polygon, &result, &err ) );
        REQUIRE( result.wavelengths.size() == 2 );
        CHECK( result.wavelengths[0] == 490.0 );
        CHECK( result.wavelengths[1] == 665.0 );
    }

    SECTION( "Degenerate polygon is rejected" )
    {
        QPolygonF polygon;
        polygon << QPointF( 0.0, 0.0 ) << QPointF( 1.0, 1.0 );
        SpectralRoiProfile::RoiProfileResult result;
        QString err;
        CHECK_FALSE( SpectralRoiProfile::meanSpectrum( path, polygon, &result, &err ) );
        CHECK_FALSE( err.isEmpty() );
    }

    SECTION( "Missing raster is rejected" )
    {
        QPolygonF polygon;
        polygon << QPointF( 0.0, 0.0 ) << QPointF( 2.0, 0.0 )
                << QPointF( 2.0, -2.0 ) << QPointF( 0.0, -2.0 );
        SpectralRoiProfile::RoiProfileResult result;
        QString err;
        CHECK_FALSE( SpectralRoiProfile::meanSpectrum(
            QStringLiteral( "/nonexistent/roi.tif" ), polygon, &result, &err ) );
    }

    SECTION( "Empty ROI yields NaN means" )
    {
        // A polygon entirely outside the raster.
        QPolygonF polygon;
        polygon << QPointF( 100.0, 100.0 ) << QPointF( 102.0, 100.0 )
                << QPointF( 102.0, 98.0 ) << QPointF( 100.0, 98.0 );
        SpectralRoiProfile::RoiProfileResult result;
        QString err;
        REQUIRE( SpectralRoiProfile::meanSpectrum( path, polygon, &result, &err ) );
        CHECK( result.pixelCount == 0 );
        CHECK( std::isnan( result.mean[0] ) );
    }
}
