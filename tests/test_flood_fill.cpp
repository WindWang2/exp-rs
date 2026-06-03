// Phase 10A Task 10.7 — RsFloodFill unit tests.
//
// Validates 4-connected BFS flood fill with L2 spectral distance threshold:
//   1. uniform 8x8 block in image center → exactly 64 pixels selected when
//      tolerance exceeds 0 (seed sits inside the block);
//   2. a single bright pixel surrounded by uniform background → tolerance
//      below the spectral gap yields only the seed pixel (1).
#include <catch2/catch_test_macros.hpp>

#include "rs_flood_fill.h"

#include <opencv2/core.hpp>

TEST_CASE( "FloodFill: uniform 8x8 block in center fills 64", "[classify][flood]" )
{
  cv::Mat img = cv::Mat::zeros( 32, 32, CV_32FC3 );
  img.setTo( cv::Scalar( 50, 50, 50 ) );
  cv::Mat roi = img( cv::Rect( 12, 12, 8, 8 ) );
  roi.setTo( cv::Scalar( 100, 100, 100 ) );

  auto pixels = RsFloodFill::run( img, 16, 16, /*tolerance=*/10.0 );
  REQUIRE( pixels.size() == 64 );
}

TEST_CASE( "FloodFill: tolerance below distance yields single pixel", "[classify][flood]" )
{
  cv::Mat img( 32, 32, CV_32FC3 );
  img.setTo( cv::Scalar( 50, 50, 50 ) );
  img.at<cv::Vec3f>( 16, 16 ) = cv::Vec3f( 100, 100, 100 );
  auto pixels = RsFloodFill::run( img, 16, 16, /*tolerance=*/1.0 );
  REQUIRE( pixels.size() == 1 );
}
