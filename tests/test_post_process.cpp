// Classification post-process pure operator tests.
#include <catch2/catch_test_macros.hpp>

#include "rs_post_process.h"

#include <opencv2/core.hpp>

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
