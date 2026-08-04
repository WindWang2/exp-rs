// Classification post-process pure operator tests.
#include <catch2/catch_test_macros.hpp>

#include "rs_post_process.h"

#include <QColor>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <opencv2/core.hpp>

#include <limits>
#include <vector>

TEST_CASE( "PostProcess: recode maps ids", "[classify][post]" )
{
  cv::Mat src = ( cv::Mat_<int>( 2, 2 ) << 1, 1, 2, 2 );
  cv::Mat dst;
  QMap<int, int> m;
  m[1] = 10;
  m[2] = 20;
  REQUIRE( RsPostProcess::recode( src, dst, m, nullptr ) );
  REQUIRE( dst.at<int>( 0, 0 ) == 10 );
  REQUIRE( dst.at<int>( 1, 1 ) == 20 );
}

TEST_CASE( "PostProcess: majority 3x3 smooths single speck", "[classify][post]" )
{
  cv::Mat src( 5, 5, CV_32S, cv::Scalar( 1 ) );
  src.at<int>( 2, 2 ) = 9; // speck
  cv::Mat dst;
  REQUIRE( RsPostProcess::majorityFilter( src, dst, 3, nullptr ) );
  REQUIRE( dst.at<int>( 2, 2 ) == 1 );
}

TEST_CASE( "PostProcess: sieve removes small component", "[classify][post]" )
{
  cv::Mat src( 10, 10, CV_32S, cv::Scalar( 1 ) );
  src.at<int>( 0, 0 ) = 2; // 1-pixel class 2
  cv::Mat dst;
  REQUIRE( RsPostProcess::sieve( src, dst, 2, 8, nullptr ) );
  REQUIRE( dst.at<int>( 0, 0 ) != 2 ); // replaced
}

namespace
{

/// Write \a labels via saveLabelRaster, reopen and return the band dtype.
GDALDataType writeAndReopen( const QString &path, const cv::Mat &labels,
                             double nodataValue, GDALDatasetH &out )
{
  double gt[6] = { 0, 1, 0, static_cast<double>( labels.rows ), 0, -1 };
  if ( !RsPostProcess::saveLabelRaster( path, labels, gt, QString(),
                                        QVector<QRgb>(), QStringList(),
                                        nodataValue, nullptr ) )
    return GDT_Unknown;
  out = GDALOpenEx( path.toUtf8().constData(), GDAL_OF_RASTER,
                    nullptr, nullptr, nullptr );
  REQUIRE( out != nullptr );
  return GDALGetRasterDataType( GDALGetRasterBand( out, 1 ) );
}

} // namespace

// ADR 0055: saveLabelRaster is the canonical class-map writer — it owns the
// ADR 0019 S4 dtype escalation (Byte <= 255, UInt16 <= 65535, Int32 beyond),
// attaches a palette only when one is given, and sets the NoData marker only
// when asked (NaN = none).
TEST_CASE( "PostProcess: saveLabelRaster dtype escalation + no-palette", "[classify][post]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  SECTION( "labels <= 255 → Byte, no palette, NoData marker set" )
  {
    cv::Mat labels( 2, 2, CV_32S );
    labels.setTo( 0 );
    labels.at<int>( 0, 0 ) = 1;
    labels.at<int>( 1, 1 ) = 200;

    GDALDatasetH ds = nullptr;
    const GDALDataType dt = writeAndReopen( tmp.path() + "/byte.tif", labels, 0.0, ds );
    CHECK( dt == GDT_Byte );
    // No palette requested → band must not be palette-indexed.
    CHECK( GDALGetRasterColorInterpretation( GDALGetRasterBand( ds, 1 ) )
           != GCI_PaletteIndex );
    int success = 0;
    CHECK( GDALGetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), &success ) == 0.0 );
    CHECK( success == 1 );
    GDALClose( ds );
  }

  SECTION( "labels in 256..65535 → UInt16" )
  {
    cv::Mat labels( 1, 2, CV_32S );
    labels.at<int>( 0, 0 ) = 255;
    labels.at<int>( 0, 1 ) = 300;

    GDALDatasetH ds = nullptr;
    const GDALDataType dt = writeAndReopen( tmp.path() + "/u16.tif", labels,
                                            std::numeric_limits<double>::quiet_NaN(), ds );
    CHECK( dt == GDT_UInt16 );
    std::vector<int> px( 2 );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Read, 0, 0, 2, 1,
                           px.data(), 2, 1, GDT_Int32, 0, 0 ) == CE_None );
    CHECK( px[0] == 255 );
    CHECK( px[1] == 300 ); // escalated, never clamped
    GDALClose( ds );
  }

  SECTION( "labels > 65535 → Int32" )
  {
    cv::Mat labels( 1, 2, CV_32S );
    labels.at<int>( 0, 0 ) = 70000;
    labels.at<int>( 0, 1 ) = -1; // negative forces Int32 too

    GDALDatasetH ds = nullptr;
    const GDALDataType dt = writeAndReopen( tmp.path() + "/i32.tif", labels,
                                            std::numeric_limits<double>::quiet_NaN(), ds );
    CHECK( dt == GDT_Int32 );
    GDALClose( ds );
  }

  SECTION( "NaN nodataValue → no NoData marker" )
  {
    cv::Mat labels( 1, 1, CV_32S, cv::Scalar( 7 ) );

    GDALDatasetH ds = nullptr;
    const GDALDataType dt = writeAndReopen( tmp.path() + "/nond.tif", labels,
                                            std::numeric_limits<double>::quiet_NaN(), ds );
    CHECK( dt == GDT_Byte );
    int success = 0;
    GDALGetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), &success );
    CHECK( success == 0 );
    GDALClose( ds );
  }
}

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include <QFile>

TEST_CASE( "RsMajorityFilterOperator and RsRecodeOperator end-to-end execution", "[classify][post][operators]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString inputPath = tmp.path() + "/test_labels.tif";
  const QString majPath = tmp.path() + "/test_maj.tif";
  const QString recodePath = tmp.path() + "/test_recode.tif";

  // Create a 5x5 label raster with a speckle pixel at (2,2)
  cv::Mat labels( 5, 5, CV_32S, cv::Scalar( 1 ) );
  labels.at<int>( 2, 2 ) = 9;

  double gt[6] = { 0, 1, 0, 5, 0, -1 };
  QString err;
  REQUIRE( RsPostProcess::saveLabelRaster( inputPath, labels, gt, QString(),
                                           QVector<QRgb>(), QStringList(),
                                           std::numeric_limits<double>::quiet_NaN(), &err ) );

  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  REQUIRE( registry.hasOperator( "rs:majority_filter" ) );
  REQUIRE( registry.hasOperator( "rs:recode" ) );

  // 1. Run Majority Filter Operator
  auto majOp = registry.create( "rs:majority_filter" );
  REQUIRE( majOp != nullptr );

  Json::Value majParams( Json::objectValue );
  majParams["input"] = inputPath.toStdString();
  majParams["output"] = majPath.toStdString();
  majParams["kernel"] = 3;

  sicnu::operators::RSOperatorContext ctx;
  Json::Value majResult = majOp->run( majParams, ctx );
  REQUIRE( majResult.isMember( "output" ) );
  REQUIRE( QFile::exists( majPath ) );

  // Verify majority filter smoothed out the speckle pixel at (2,2)
  cv::Mat filteredLabels;
  double outGt[6];
  QString outWkt;
  REQUIRE( RsPostProcess::loadLabelRaster( majPath, filteredLabels, outGt, outWkt, &err ) );
  CHECK( filteredLabels.at<int>( 2, 2 ) == 1 );

  // 2. Run Recode Operator (remap label 1 to label 10)
  auto recodeOp = registry.create( "rs:recode" );
  REQUIRE( recodeOp != nullptr );

  Json::Value recodeParams( Json::objectValue );
  recodeParams["input"] = majPath.toStdString();
  recodeParams["output"] = recodePath.toStdString();
  Json::Value map( Json::objectValue );
  map["1"] = 10;
  recodeParams["recode_map"] = map;

  Json::Value recodeResult = recodeOp->run( recodeParams, ctx );
  REQUIRE( recodeResult.isMember( "output" ) );
  REQUIRE( QFile::exists( recodePath ) );

  cv::Mat recodedLabels;
  REQUIRE( RsPostProcess::loadLabelRaster( recodePath, recodedLabels, outGt, outWkt, &err ) );
  CHECK( recodedLabels.at<int>( 0, 0 ) == 10 );
  CHECK( recodedLabels.at<int>( 2, 2 ) == 10 );
}
