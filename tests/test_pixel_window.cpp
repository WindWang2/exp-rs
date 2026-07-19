// tests/test_pixel_window.cpp — Classification v1.1 viewport → pixel window
#include <catch2/catch_test_macros.hpp>

#include "qgsrectangle.h"
#include "rs_pixel_window.h"

TEST_CASE( "PixelWindow: identity GT maps extent to pixels", "[classify][window]" )
{
  // North-up: origin (0, 100), pixel size 1×-1, 100×100 raster.
  double gt[6] = { 0, 1, 0, 100, 0, -1 };
  QgsRectangle ext( 10, 40, 30, 80 ); // xmin, ymin, xmax, ymax
  const auto w = rsMapExtentToPixelWindow( ext, gt, 100, 100 );
  REQUIRE( w.valid );
  REQUIRE( w.x0 == 10 );
  REQUIRE( w.x1 == 30 );
  // y: map 80 → row 20, map 40 → row 60
  REQUIRE( w.y0 == 20 );
  REQUIRE( w.y1 == 60 );
  REQUIRE( w.width() == 20 );
  REQUIRE( w.height() == 40 );
}

TEST_CASE( "PixelWindow: disjoint extent invalid", "[classify][window]" )
{
  double gt[6] = { 0, 1, 0, 100, 0, -1 };
  QgsRectangle ext( 1000, 1000, 1100, 1100 );
  const auto w = rsMapExtentToPixelWindow( ext, gt, 100, 100 );
  REQUIRE_FALSE( w.valid );
}

TEST_CASE( "PixelWindow: partial overlap clamps to raster", "[classify][window]" )
{
  double gt[6] = { 0, 1, 0, 100, 0, -1 };
  // Overlaps left edge: map x -10..20 → pixels clamp to 0..20
  QgsRectangle ext( -10, 40, 20, 80 );
  const auto w = rsMapExtentToPixelWindow( ext, gt, 100, 100 );
  REQUIRE( w.valid );
  REQUIRE( w.x0 == 0 );
  REQUIRE( w.x1 == 20 );
  REQUIRE( w.y0 == 20 );
  REQUIRE( w.y1 == 60 );
}
