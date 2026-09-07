// test_nodata_utils.cpp — shared NoData sentinel policy (Foundation 4.0 A1):
// declared sentinel resolution, NaN fallback, and exact-match validity.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include "processing/algorithms/nodata_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using sicnu::rs::bandNoDataSentinel;
using sicnu::rs::isNoDataValue;

namespace
{
int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_nodata_utils";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

/// 3x2 float raster in a per-test temp dir; band 1 carries a declared nodata
/// value only when @p declareNodata is set.
GdalDatasetWrapper makeRaster( const QTemporaryDir &dir, const QString &name,
                               bool declareNodata, double nodata )
{
    ensureApp();
    ensureGdalInit();
    const std::array<double, 6> gt = { 500000.0, 30.0, 0.0, 4500000.0, 0.0, -30.0 };
    QString error;
    GDALDatasetH ds = createOutputTiff( dir.filePath( name ), 3, 2, 1, GDT_Float32, gt,
                                        QStringLiteral( "EPSG:32648" ), &error );
    REQUIRE( ds != nullptr );
    if ( declareNodata )
        REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), nodata ) == CE_None );
    GDALClose( ds );

    GdalDatasetWrapper wrapper;
    REQUIRE( wrapper.open( dir.filePath( name ) ) );
    return wrapper;
}
} // namespace

TEST_CASE( "bandNoDataSentinel resolves declared sentinels and NaN fallback",
           "[processing][nodata]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    GdalDatasetWrapper declared = makeRaster( dir, QStringLiteral( "declared.tif" ), true, -9999.0 );
    REQUIRE( bandNoDataSentinel( declared, 1 ) == -9999.0f );

    GdalDatasetWrapper undeclared =
        makeRaster( dir, QStringLiteral( "undeclared.tif" ), false, 0.0 );
    REQUIRE( std::isnan( bandNoDataSentinel( undeclared, 1 ) ) );
}

TEST_CASE( "isNoDataValue matches exactly and never epsilon-drops near values",
           "[processing][nodata]" )
{
    // Declared sentinel: exact float-cast match, nothing else.
    REQUIRE( isNoDataValue( -9999.0f, -9999.0f ) );
    // A representable neighbour of the sentinel is data, never dropped by an
    // epsilon (1/8192 would round back onto -9999.0f; ULP there is 1/1024).
    REQUIRE_FALSE( isNoDataValue( 10.0f + 1.0e-4f, 10.0f ) );
    REQUIRE( isNoDataValue( 5.0f, 5.0f ) );
    REQUIRE_FALSE( isNoDataValue( 5.0f, 6.0f ) );

    // NaN pixels are invalid under every sentinel.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE( isNoDataValue( nan, -9999.0f ) );
    REQUIRE( isNoDataValue( nan, nan ) );
    REQUIRE( isNoDataValue( nan, std::numeric_limits<float>::quiet_NaN() ) );

    // Non-NaN values against the undeclared (NaN) sentinel are data.
    REQUIRE_FALSE( isNoDataValue( 0.0f, nan ) );
    REQUIRE_FALSE( isNoDataValue( -9999.0f, nan ) );
}
