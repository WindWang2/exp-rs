// test_pixel_window.cpp — Classification v1.1 Task 6
//
// Verifies map-extent → half-open pixel window conversion for north-up GT
// and that disjoint extents are marked invalid.
#include "rs_pixel_window.h"
#include "qgsrectangle.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE( "PixelWindow: identity GT maps extent to pixels", "[classify][window]" )
{
  double gt[6] = { 0, 1, 0, 100, 0, -1 }; // north-up
  QgsRectangle ext( 10, 40, 30, 80 ); // xmin,ymin,xmax,ymax
  const auto w = rsMapExtentToPixelWindow( ext, gt, 100, 100 );
  REQUIRE( w.valid );
  REQUIRE( w.x0 == 10 );
  REQUIRE( w.x1 == 30 );
  // y: map 80 → row 20, map 40 → row 60
  REQUIRE( w.y0 == 20 );
  REQUIRE( w.y1 == 60 );
}

TEST_CASE( "PixelWindow: disjoint extent invalid", "[classify][window]" )
{
  double gt[6] = { 0, 1, 0, 100, 0, -1 };
  QgsRectangle ext( 1000, 1000, 1100, 1100 );
  const auto w = rsMapExtentToPixelWindow( ext, gt, 100, 100 );
  REQUIRE_FALSE( w.valid );
}
