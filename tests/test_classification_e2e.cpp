// Phase 10A Task 10.8 — End-to-end classification test.
//
// Build a 32x32 3-band synthetic raster with 3 distinct regions, train a
// NormalBayes backend on 30 hand-labelled pixels (10 / class), run the task
// synchronously and spot-check the output GeoTIFF.
#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QFile>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include "rs_classification_task.h"
#include "rs_classifier_normalbayes.h"

TEST_CASE(
  "Classification E2E: 32x32 raster + 3 classes produces valid GeoTIFF",
  "[classify][e2e]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString srcPath = tmp.path() + "/src.tif";

  // --- Build synthetic 32x32 3-band raster ----------------------------------
  // Region layout (top-left origin, GDAL GT y-pixel-size = -1 → row 0 = top):
  //   (r<16, c<16)  → region 0  → band-0 ≈ 200, others ≈ 20
  //   (r<16, c>=16) → region 1  → band-1 ≈ 200, others ≈ 20
  //   (r>=16)       → region 2  → band-2 ≈ 200, others ≈ 20
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( srcPath.toUtf8().constData(),
                                 32, 32, 3, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );

  std::vector<float> band( 32 * 32 );
  for ( int b = 0; b < 3; ++b )
  {
    for ( int r = 0; r < 32; ++r )
    {
      for ( int c = 0; c < 32; ++c )
      {
        const int region = ( r < 16 && c < 16 ) ? 0
                                                : ( r < 16 ? 1 : 2 );
        band[r * 32 + c] = ( b == region ? 200.0f : 20.0f )
                           + static_cast<float>( ( r * 7 + c * 3 + b ) % 5 );
      }
    }
    ds->GetRasterBand( b + 1 )->RasterIO(
      GF_Write, 0, 0, 32, 32, band.data(), 32, 32, GDT_Float32, 0, 0 );
  }
  double gt[6] = { 0, 1, 0, 32, 0, -1 };
  ds->SetGeoTransform( gt );
  GDALClose( ds );

  // --- Build training data: 10 hand-labelled pixels per class ---------------
  cv::Mat X( 30, 3, CV_32F ), y( 30, 1, CV_32S );
  for ( int i = 0; i < 10; ++i )
  {
    X.at<float>( i, 0 ) = 200.0f;
    X.at<float>( i, 1 ) = 20.0f;
    X.at<float>( i, 2 ) = 20.0f;
    y.at<int>( i, 0 ) = 1;
  }
  for ( int i = 0; i < 10; ++i )
  {
    X.at<float>( 10 + i, 0 ) = 20.0f;
    X.at<float>( 10 + i, 1 ) = 200.0f;
    X.at<float>( 10 + i, 2 ) = 20.0f;
    y.at<int>( 10 + i, 0 ) = 2;
  }
  for ( int i = 0; i < 10; ++i )
  {
    X.at<float>( 20 + i, 0 ) = 20.0f;
    X.at<float>( 20 + i, 1 ) = 20.0f;
    X.at<float>( 20 + i, 2 ) = 200.0f;
    y.at<int>( 20 + i, 0 ) = 3;
  }

  // --- Build the task config ------------------------------------------------
  RsClassificationTask::Config cfg;
  cfg.sourceRaster = srcPath;
  cfg.outputRaster = tmp.path() + "/out.tif";
  cfg.bandIndices = { 1, 2, 3 };
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainX = X;
  cfg.trainY = y;
  cfg.classColors[1] = QColor( "#cc0000" );
  cfg.classColors[2] = QColor( "#00cc00" );
  cfg.classColors[3] = QColor( "#0000cc" );
  cfg.algoName = QStringLiteral( "NormalBayes" );

  RsClassificationTask task( std::move( cfg ) );
  REQUIRE( task.run() );
  REQUIRE( task.result().ok );
  REQUIRE( QFile::exists( tmp.path() + "/out.tif" ) );
  REQUIRE( task.result().totalPixels == 32 * 32 );

  // --- Spot-check output pixels ---------------------------------------------
  GDALDataset *outDs = static_cast<GDALDataset *>(
    GDALOpen( ( tmp.path() + "/out.tif" ).toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( outDs != nullptr );

  uint8_t tl = 0, tr = 0, br = 0;
  outDs->GetRasterBand( 1 )->RasterIO( GF_Read, 2, 2, 1, 1, &tl, 1, 1, GDT_Byte, 0, 0 );
  outDs->GetRasterBand( 1 )->RasterIO( GF_Read, 28, 2, 1, 1, &tr, 1, 1, GDT_Byte, 0, 0 );
  outDs->GetRasterBand( 1 )->RasterIO( GF_Read, 16, 28, 1, 1, &br, 1, 1, GDT_Byte, 0, 0 );

  REQUIRE( static_cast<int>( tl ) == 1 );
  REQUIRE( static_cast<int>( tr ) == 2 );
  REQUIRE( static_cast<int>( br ) == 3 );

  // ColorTable should be present
  GDALColorTable *ct = outDs->GetRasterBand( 1 )->GetColorTable();
  REQUIRE( ct != nullptr );

  GDALClose( outDs );
}
