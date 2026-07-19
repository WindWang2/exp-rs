// test_hungarian_assignment.cpp — Phase 10A.1.1 / 10A.3.
//
// Munkres O(n^3) Hungarian min-cost assignment on square and rectangular
// cost matrices (pad with 1e9 when n != m).

#include <catch2/catch_test_macros.hpp>
#include "rs_hungarian_assignment.h"
#include <opencv2/core.hpp>

TEST_CASE( "Hungarian: 3x3 identity cost yields identity", "[classify][hungarian]" )
{
  cv::Mat cost = ( cv::Mat_<double>( 3, 3 ) <<
                   0, 1, 1,
                   1, 0, 1,
                   1, 1, 0 );
  auto a = RsHungarianAssignment::solve( cost );
  REQUIRE( a.size() == 3 );
  REQUIRE( a[0] == 0 );
  REQUIRE( a[1] == 1 );
  REQUIRE( a[2] == 2 );
}

TEST_CASE( "Hungarian: 3x3 off-diagonal optimum", "[classify][hungarian]" )
{
  // Best is to assign row0->col2, row1->col0, row2->col1 (total 0).
  cv::Mat cost = ( cv::Mat_<double>( 3, 3 ) <<
                   9, 9, 0,
                   0, 9, 9,
                   9, 0, 9 );
  auto a = RsHungarianAssignment::solve( cost );
  REQUIRE( a[0] == 2 );
  REQUIRE( a[1] == 0 );
  REQUIRE( a[2] == 1 );
}

TEST_CASE( "Hungarian: 1x1 trivial", "[classify][hungarian]" )
{
  cv::Mat cost = ( cv::Mat_<double>( 1, 1 ) << 5.0 );
  auto a = RsHungarianAssignment::solve( cost );
  REQUIRE( a.size() == 1 );
  REQUIRE( a[0] == 0 );
}

TEST_CASE( "Hungarian: 6x6 diagonal-dominant returns identity", "[classify][hungarian]" )
{
  cv::Mat cost = cv::Mat::ones( 6, 6, CV_64F ) * 10.0;
  for ( int i = 0; i < 6; ++i )
    cost.at<double>( i, i ) = 0.0;
  auto a = RsHungarianAssignment::solve( cost );
  for ( int i = 0; i < 6; ++i )
    REQUIRE( a[i] == i );
}

TEST_CASE( "Hungarian: empty matrix returns empty vector", "[classify][hungarian]" )
{
  cv::Mat cost;
  auto a = RsHungarianAssignment::solve( cost );
  REQUIRE( a.isEmpty() );
}

TEST_CASE( "Hungarian: 2x3 rectangular maps rows to real columns", "[classify][hungarian]" )
{
  // 2 true classes, 3 clusters; best: class0->c0, class1->c1
  cv::Mat cost = ( cv::Mat_<double>( 2, 3 ) <<
                   0, 5, 5,
                   5, 0, 5 );
  auto a = RsHungarianAssignment::solve( cost );
  REQUIRE( a.size() == 2 );
  REQUIRE( a[0] == 0 );
  REQUIRE( a[1] == 1 );
}

TEST_CASE( "Hungarian: 3x2 rectangular", "[classify][hungarian]" )
{
  // 3 true classes, 2 clusters; first two rows take the real columns,
  // third row is assigned to a pad column → -1.
  cv::Mat cost = ( cv::Mat_<double>( 3, 2 ) <<
                   0, 5,
                   5, 0,
                   5, 5 );
  auto a = RsHungarianAssignment::solve( cost );
  REQUIRE( a.size() == 3 );
  REQUIRE( a[0] >= 0 );
  REQUIRE( a[1] >= 0 );
  REQUIRE( a[0] != a[1] );
  REQUIRE( ( a[0] == 0 || a[0] == 1 ) );
  REQUIRE( ( a[1] == 0 || a[1] == 1 ) );
  REQUIRE( a[2] == -1 );
}
