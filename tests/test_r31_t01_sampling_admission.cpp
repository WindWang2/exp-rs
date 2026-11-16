#include <catch2/catch_test_macros.hpp>

#include "rs_training_data_extraction.h"
#include "qgsgeometry.h"

#include <QTemporaryDir>

#include <gdal_priv.h>

#include <vector>

namespace
{

void writeRaster( const QString &path, int width, int height )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                     GDT_Float32, nullptr );
  REQUIRE( dataset != nullptr );
  std::vector<float> pixels( static_cast<size_t>( width ) * height );
  for ( int i = 0; i < width * height; ++i )
    pixels[static_cast<size_t>( i )] = static_cast<float>( i + 1 );
  REQUIRE( GDALRasterIO( GDALGetRasterBand( dataset, 1 ), GF_Write, 0, 0, width, height,
                         pixels.data(), width, height, GDT_Float32, 0, 0 ) == CE_None );
  double transform[6] = { 0.0, 1.0, 0.0, static_cast<double>( height ), 0.0, -1.0 };
  REQUIRE( GDALSetGeoTransform( dataset, transform ) == CE_None );
  GDALClose( dataset );
}

RsTrainingGeometry fullGeometry( int width, int height, int classId )
{
  RsTrainingGeometry geometry;
  geometry.classId = classId;
  geometry.geometry = QgsGeometry::fromWkt(
    QStringLiteral( "POLYGON((0 0, %1 0, %1 %2, 0 %2, 0 0))" ).arg( width ).arg( height ) );
  return geometry;
}

} // namespace

TEST_CASE( "R31-T01 WP11: a tiny candidate budget refuses before a full ROI set is consumed",
           "[r31][t01][sampling_admission][budget]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = dir.filePath( QStringLiteral( "roi.tif" ) );
  writeRaster( raster, 64, 64 );
  RsTrainingDataExtraction::Options options;
  options.maxCandidates = 4;
  const auto result = RsTrainingDataExtraction::extract(
    raster, { 1 }, { fullGeometry( 64, 64, 1 ) }, options );
  CHECK( result.error == RsTrainingDataResult::Error::BudgetExceeded );
  CHECK_FALSE( result.ok );
  CHECK( result.errorMessage.contains( QStringLiteral( "budget" ) ) );
  CHECK( result.X.empty() );
  CHECK( result.y.empty() );
  CHECK( result.sampleGroupIds.empty() );
}

TEST_CASE( "R31-T01 WP11: admitted samples preserve values, order and last-geometry-wins",
           "[r31][t01][sampling_admission][equivalence]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = dir.filePath( QStringLiteral( "small.tif" ) );
  writeRaster( raster, 2, 2 );
  RsTrainingDataExtraction::Options options;
  options.maxCandidates = 4;
  const auto result = RsTrainingDataExtraction::extract(
    raster, { 1 }, { fullGeometry( 2, 2, 1 ), fullGeometry( 2, 2, 2 ) }, options );
  REQUIRE( result.ok );
  REQUIRE( result.X.rows == 4 );
  REQUIRE( result.y.rows == 4 );
  REQUIRE( result.sampleGroupIds.size() == 4 );
  for ( int row = 0; row < 4; ++row )
  {
    CHECK( result.X.at<float>( row, 0 ) == static_cast<float>( row + 1 ) );
    CHECK( result.y.at<int>( row, 0 ) == 2 );
    CHECK( result.sampleGroupIds[static_cast<size_t>( row )] == 1 );
  }
  CHECK( result.classCounts.value( 2 ) == 4 );
}
