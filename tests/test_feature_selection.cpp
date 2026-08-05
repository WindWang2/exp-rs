// OBIA Classification Optimization — RsFeatureSelection unit test.
#include <catch2/catch_test_macros.hpp>

#include <opencv2/core.hpp>

#include "rs_segment_features.h"

TEST_CASE( "FeatureSelection: Selective feature matrix building",
           "[obia][features][selection]" )
{
  QMap<quint32, RsSegmentFeatures::SegmentStat> stats;

  RsSegmentFeatures::SegmentStat s1;
  s1.mean = { 10.0, 20.0 };
  s1.stddev = { 1.0, 2.0 };
  s1.min = { 5.0, 15.0 };
  s1.max = { 15.0, 25.0 };
  s1.area = 100.0;
  s1.perimeter = 40.0;
  s1.shapeIndex = 1.0;
  s1.compactness = 1.0;
  s1.rectangularity = 1.0;
  s1.aspectRatio = 1.0;

  stats[1] = s1;

  // 1. Full feature matrix (mean, std, min, max, GLCM * 4 per band + 6 shape = 2 * 8 + 6 = 22)
  QVector<quint32> segIds;
  RsFeatureSelection fullSel;
  cv::Mat fullMat = RsSegmentFeatures::toFeatureMatrix( stats, segIds, fullSel );
  REQUIRE( fullMat.rows == 1 );
  REQUIRE( fullMat.cols == 22 );

  // 2. Select only Spectral Mean + Area (2 * 1 + 1 = 3 features)
  RsFeatureSelection customSel;
  customSel.useMean = true;
  customSel.useStdDev = false;
  customSel.useMin = false;
  customSel.useMax = false;
  customSel.useGlcmContrast = false;
  customSel.useGlcmCorrelation = false;
  customSel.useGlcmEnergy = false;
  customSel.useGlcmHomogeneity = false;
  customSel.useArea = true;
  customSel.usePerimeter = false;
  customSel.useShapeIndex = false;
  customSel.useCompactness = false;
  customSel.useRectangularity = false;
  customSel.useAspectRatio = false;

  cv::Mat customMat = RsSegmentFeatures::toFeatureMatrix( stats, segIds, customSel );
  REQUIRE( customMat.rows == 1 );
  REQUIRE( customMat.cols == 3 );
  REQUIRE( customMat.at<float>( 0, 0 ) == 10.0f );
  REQUIRE( customMat.at<float>( 0, 1 ) == 20.0f );
  REQUIRE( customMat.at<float>( 0, 2 ) == 100.0f );
}
