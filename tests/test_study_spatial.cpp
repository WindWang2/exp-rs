// test_study_spatial.cpp — spatial difference summary contracts (RS14-07 Slice E).
//
// Buffer-level oracles are hand-computed; the GDAL adapter is exercised on
// tiny synthetic GeoTIFFs written in-process (offline, no fixtures on disk).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "study/study_spatial.h"

#include <QTemporaryDir>

#include <cmath>
#include <limits>
#include <vector>

#include "gdal_priv.h"
#include "ogr_spatialref.h"

using namespace sicnu::study;

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
} // namespace

TEST_CASE( "buffer difference summary is exact on hand-computed inputs",
           "[study][spatial]" )
{
    // 6 pixels: [10 10 10 NaN 5 7] vs [10 12 15 NaN NaN 7], epsilon 1.0
    const std::vector<float> baseline = { 10.f, 10.f, 10.f, kNan, 5.f, 7.f };
    const std::vector<float> run = { 10.f, 12.f, 15.f, kNan, kNan, 7.f };
    const SpatialDifferenceSummary summary = summarizeBufferDifference(
        QStringLiteral( "base.tif" ), QStringLiteral( "run.tif" ), baseline.data(),
        run.data(), static_cast<qint64>( baseline.size() ), 1.0 );

    REQUIRE( summary.totalPixels == 6 );
    REQUIRE( summary.validPixels == 4 ); // NaN in either side invalidates (idx 3, 4)
    REQUIRE( summary.changedPixels == 2 ); // |10−12| = 2 and |10−15| = 5 both > 1
    REQUIRE( summary.changedPercent == Catch::Approx( 50.0 ) );
    REQUIRE( summary.meanAbsDiff == Catch::Approx( ( 0.0 + 2.0 + 5.0 + 0.0 ) / 4.0 ) );
    REQUIRE( summary.maxAbsDiff == Catch::Approx( 5.0 ) );
    REQUIRE( summary.rmsDiff
             == Catch::Approx( std::sqrt( ( 0.0 + 4.0 + 25.0 + 0.0 ) / 4.0 ) ) );
}

TEST_CASE( "identical buffers produce a zero summary", "[study][spatial]" )
{
    const std::vector<float> baseline = { 1.f, 2.f, 3.f };
    const std::vector<float> run = { 1.f, 2.f, 3.f };
    const SpatialDifferenceSummary summary = summarizeBufferDifference(
        QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ), baseline.data(), run.data(),
        3, 0.0 );
    REQUIRE( summary.validPixels == 3 );
    REQUIRE( summary.changedPixels == 0 );
    REQUIRE( summary.changedPercent == 0.0 );
    REQUIRE( summary.meanAbsDiff == 0.0 );
}

TEST_CASE( "an all-invalid buffer never divides by zero", "[study][spatial][boundary]" )
{
    const std::vector<float> baseline = { kNan, kNan };
    const std::vector<float> run = { 1.f, 2.f };
    const SpatialDifferenceSummary summary = summarizeBufferDifference(
        QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ), baseline.data(), run.data(),
        2, 0.5 );
    REQUIRE( summary.validPixels == 0 );
    REQUIRE( summary.changedPercent == 0.0 );
    REQUIRE( summary.meanAbsDiff == 0.0 );
    REQUIRE( summary.rmsDiff == 0.0 );
}

TEST_CASE( "epsilon gates the changed-pixel count", "[study][spatial][boundary]" )
{
    const std::vector<float> baseline = { 1.0f, 1.0f };
    const std::vector<float> run = { 1.05f, 1.15f };
    const auto atBoundary = summarizeBufferDifference(
        QStringLiteral( "a" ), QStringLiteral( "b" ), baseline.data(), run.data(), 2, 0.05 );
    // |diff| == epsilon is NOT a change (strictly greater contract).
    REQUIRE( atBoundary.changedPixels == 1 );
}

TEST_CASE( "spatial summary JSON roundtrips and refuses foreign versions",
           "[study][spatial]" )
{
    const std::vector<float> baseline = { 0.f, 4.f };
    const std::vector<float> run = { 1.f, 2.f };
    const auto summary = summarizeBufferDifference(
        QStringLiteral( "a" ), QStringLiteral( "b" ), baseline.data(), run.data(), 2, 0.5 );
    const auto json = summary.toJson();
    const auto parsed = SpatialDifferenceSummary::fromJson( json );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed.value() == summary );

    auto foreign = json;
    foreign.insert( QStringLiteral( "schema_version" ), 99 );
    REQUIRE( !SpatialDifferenceSummary::fromJson( foreign ).has_value() );
}

namespace
{

void writeTinyTif( const QString &path, const std::vector<float> &pixels, int width,
                   int height, const double *geotransform, const char *wkt, double nodata )
{
    GDALAllRegister();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *dataset =
        driver->Create( path.toUtf8().constData(), width, height, 1, GDT_Float32, nullptr );
    REQUIRE( dataset != nullptr );
    if ( geotransform )
        dataset->SetGeoTransform( const_cast<double *>( geotransform ) );
    if ( wkt )
    {
        OGRSpatialReference crs;
        REQUIRE( crs.importFromWkt( wkt ) == OGRERR_NONE );
        dataset->SetSpatialRef( &crs );
    }
    GDALRasterBand *band = dataset->GetRasterBand( 1 );
    REQUIRE( band != nullptr );
    if ( !std::isnan( nodata ) )
        band->SetNoDataValue( nodata );
    REQUIRE( band->RasterIO( GF_Write, 0, 0, width, height,
                             const_cast<float *>( pixels.data() ), width, height, GDT_Float32,
                             0, 0, nullptr ) == CE_None );
    GDALClose( dataset );
}

const char *kEpsg4326Wkt =
    "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563,"
    "AUTHORITY[\"EPSG\",\"7030\"]],AUTHORITY[\"EPSG\",\"6326\"]],"
    "PRIMEM[\"Greenwich\",0,AUTHORITY[\"EPSG\",\"8901\"]],"
    "UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],"
    "AUTHORITY[\"EPSG\",\"4326\"]]";

} // namespace

TEST_CASE( "GDAL adapter summarizes synthetic geotiffs and honours nodata",
           "[study][spatial][gdal]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const double geotransform[6] = { 10.0, 1.0, 0.0, 20.0, 0.0, -1.0 };
    const QString baselinePath = dir.filePath( QStringLiteral( "baseline.tif" ) );
    const QString runPath = dir.filePath( QStringLiteral( "run.tif" ) );
    // 2x2: one pixel differs; one pixel is nodata in the run (excluded).
    writeTinyTif( baselinePath, { 1.f, 2.f, 3.f, 4.f }, 2, 2, geotransform, kEpsg4326Wkt,
                  std::nan( "" ) );
    writeTinyTif( runPath, { 1.f, 9.f, 3.f, -9999.f }, 2, 2, geotransform, kEpsg4326Wkt,
                  -9999.0 );

    GdalRasterDifferenceSummarizer summarizer( 1.0 );
    const auto summary = summarizer.summarize( baselinePath, runPath );
    REQUIRE( summary.has_value() );
    REQUIRE( summary.value().totalPixels == 4 );
    REQUIRE( summary.value().validPixels == 3 );
    REQUIRE( summary.value().changedPixels == 1 );
    REQUIRE( summary.value().maxAbsDiff == Catch::Approx( 7.0 ) );
}

TEST_CASE( "GDAL adapter refuses mismatched grids with typed errors",
           "[study][spatial][gdal]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const double geotransform[6] = { 10.0, 1.0, 0.0, 20.0, 0.0, -1.0 };
    const QString baselinePath = dir.filePath( QStringLiteral( "baseline.tif" ) );
    const QString runPath = dir.filePath( QStringLiteral( "run.tif" ) );
    writeTinyTif( baselinePath, { 1.f, 2.f, 3.f, 4.f }, 2, 2, geotransform, kEpsg4326Wkt,
                  std::nan( "" ) );

    GdalRasterDifferenceSummarizer summarizer( 1.0 );

    SECTION( "dimension mismatch" )
    {
        writeTinyTif( runPath, { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f }, 3, 3,
                      geotransform, kEpsg4326Wkt, std::nan( "" ) );
        const auto summary = summarizer.summarize( baselinePath, runPath );
        REQUIRE( !summary.has_value() );
        REQUIRE( summary.diagnostics().first().code == QStringLiteral( "study.spatial_mismatch" ) );
    }
    SECTION( "crs mismatch" )
    {
        const char *epsg32633 =
            "PROJCS[\"WGS 84 / UTM zone 33N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
            "SPHEROID[\"WGS 84\",6378137,298.257223563,AUTHORITY[\"EPSG\",\"7030\"]],"
            "AUTHORITY[\"EPSG\",\"6326\"]],PRIMEM[\"Greenwich\",0,AUTHORITY[\"EPSG\",\"8901\"]],"
            "UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],"
            "AUTHORITY[\"EPSG\",\"4326\"]],PROJECTION[\"Transverse_Mercator\"],"
            "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",15],"
            "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
            "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1,AUTHORITY[\"EPSG\",\"9001\"]],"
            "AXIS[\"Easting\",EAST],AXIS[\"Northing\",NORTH],AUTHORITY[\"EPSG\",\"32633\"]]";
        writeTinyTif( runPath, { 1.f, 2.f, 3.f, 4.f }, 2, 2, geotransform, epsg32633,
                      std::nan( "" ) );
        const auto summary = summarizer.summarize( baselinePath, runPath );
        REQUIRE( !summary.has_value() );
        REQUIRE( summary.diagnostics().first().code == QStringLiteral( "study.spatial_mismatch" ) );
    }
    SECTION( "unreadable raster" )
    {
        const auto summary =
            summarizer.summarize( baselinePath, dir.filePath( QStringLiteral( "nope.tif" ) ) );
        REQUIRE( !summary.has_value() );
        REQUIRE( summary.diagnostics().first().code == QStringLiteral( "study.spatial_unreadable" ) );
    }
}
