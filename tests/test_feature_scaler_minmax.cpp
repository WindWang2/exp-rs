// OBIA Classification Optimization — RsFeatureScaler MinMax unit test.
#include <catch2/catch_test_macros.hpp>

#include <opencv2/core.hpp>
#include <QJsonObject>

#include "rs_feature_scaler.h"

TEST_CASE( "FeatureScaler: MinMax normalization and serialization",
           "[obia][scaler][minmax]" )
{
  cv::Mat trainX = ( cv::Mat_<float>( 3, 2 ) << 10.0, 100.0,
                     20.0, 200.0,
                     30.0, 300.0 );

  RsFeatureScaler scaler;
  bool ok = scaler.fit( trainX, RsFeatureScaler::Method::MinMax );
  REQUIRE( ok );
  REQUIRE( scaler.isFitted() );
  REQUIRE( scaler.method() == RsFeatureScaler::Method::MinMax );

  cv::Mat scaled = scaler.transform( trainX );
  REQUIRE( scaled.rows == 3 );
  REQUIRE( scaled.cols == 2 );

  // Check row 0 (min) -> 0.0, row 2 (max) -> 1.0, row 1 (mid) -> 0.5
  REQUIRE( std::abs( scaled.at<float>( 0, 0 ) - 0.0f ) < 1e-5f );
  REQUIRE( std::abs( scaled.at<float>( 1, 0 ) - 0.5f ) < 1e-5f );
  REQUIRE( std::abs( scaled.at<float>( 2, 0 ) - 1.0f ) < 1e-5f );

  REQUIRE( std::abs( scaled.at<float>( 0, 1 ) - 0.0f ) < 1e-5f );
  REQUIRE( std::abs( scaled.at<float>( 1, 1 ) - 0.5f ) < 1e-5f );
  REQUIRE( std::abs( scaled.at<float>( 2, 1 ) - 1.0f ) < 1e-5f );

  // Test JSON serialization & round-trip
  QJsonObject json = scaler.toJson();
  RsFeatureScaler scaler2;
  bool restoredOk = scaler2.fromJson( json );
  REQUIRE( restoredOk );
  REQUIRE( scaler2.isFitted() );
  REQUIRE( scaler2.method() == RsFeatureScaler::Method::MinMax );

  cv::Mat scaled2 = scaler2.transform( trainX );
  REQUIRE( std::abs( scaled2.at<float>( 1, 0 ) - 0.5f ) < 1e-5f );
}
