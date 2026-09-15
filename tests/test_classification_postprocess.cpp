// tests/test_classification_postprocess.cpp — D15 Package D.
//
// Ground-truth policy: hand-constructed label rasters whose component areas,
// border-edge counts and majority counts are computed by hand in the
// comments; expected outputs are those counts, never code-derived.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include "processing/algorithms/classification_postprocess.h"

using rs::processing::ClassificationPostProcessor;
using rs::processing::MorphologicalFilterConfig;

namespace
{
  constexpr int kNoData = -1;

  // 10x10 zero background, 4x4 class-1 square at rows/cols 3..6 (16 px) plus
  // two isolated class-1 single pixels (spec scenario).
  std::vector<int> specRaster10x10()
  {
    std::vector<int> r( 100, 0 );
    for ( int y = 3; y <= 6; ++y )
      for ( int x = 3; x <= 6; ++x )
        r[static_cast<size_t>( y ) * 10 + x] = 1;
    r[static_cast<size_t>( 1 ) * 10 + 8] = 1; // isolated salt pixel
    r[static_cast<size_t>( 8 ) * 10 + 1] = 1; // isolated salt pixel
    return r;
  }

  size_t countClass( const std::vector<int> &r, int cls )
  {
    size_t n = 0;
    for ( const int v : r )
      n += ( v == cls ) ? 1 : 0;
    return n;
  }
} // namespace

TEST_CASE( "Sieve removes salt pixels but never erodes the valid core", "[d15][postprocess]" )
{
  const auto in = specRaster10x10();
  const auto out = ClassificationPostProcessor::applySieveFilter( in, 10, 10, 4, 8 );

  REQUIRE( out.size() == in.size() );
  REQUIRE( countClass( out, 1 ) == 16 );          // exactly the 4x4 core survives
  REQUIRE( out[static_cast<size_t>( 1 ) * 10 + 8] == 0 ); // salt pixel dissolved
  REQUIRE( out[static_cast<size_t>( 8 ) * 10 + 1] == 0 );
  for ( int y = 3; y <= 6; ++y )
    for ( int x = 3; x <= 6; ++x )
      REQUIRE( out[static_cast<size_t>( y ) * 10 + x] == 1 );
}

TEST_CASE( "Sieve honours connectivity on diagonal chains", "[d15][postprocess]" )
{
  // Five pixels joined only at corners: one 8-connected component of 5, but
  // five 4-connected components of 1.
  std::vector<int> r( 100, 0 );
  for ( int i = 0; i < 5; ++i )
    r[static_cast<size_t>( i ) * 10 + i] = 7;

  const auto kept8 = ClassificationPostProcessor::applySieveFilter( r, 10, 10, 4, 8 );
  REQUIRE( countClass( kept8, 7 ) == 5 ); // diagonal chain >= min size, kept

  const auto kept4 = ClassificationPostProcessor::applySieveFilter( r, 10, 10, 4, 4 );
  REQUIRE( countClass( kept4, 7 ) == 0 ); // five 1-px components all dissolved
}

TEST_CASE( "Sieve reassigns to the neighbour with the longest shared border", "[d15][postprocess]" )
{
  // 8x4: cols 0-3 class 0, cols 6-7 class 1, island of 5 class-2 pixels.
  // Island border edges (orthogonal): 3 to class 0, 2 to class 1
  // => the longest-border rule must pick class 0.
  std::vector<int> r( 32, 0 );
  for ( size_t i = 0; i < r.size(); ++i )
    r[i] = ( i % 8 >= 6 ) ? 1 : 0;
  const size_t island[] = { 0 * 8 + 4, 1 * 8 + 4, 2 * 8 + 4, 0 * 8 + 5, 1 * 8 + 5 };
  for ( const size_t idx : island )
    r[idx] = 2;

  const auto out = ClassificationPostProcessor::applySieveFilter( r, 8, 4, 6, 4 );
  for ( const size_t idx : island )
    REQUIRE( out[idx] == 0 );
}

TEST_CASE( "Sieve hands pixels to NoData when no thematic neighbour exists", "[d15][postprocess]" )
{
  std::vector<int> r( 25, kNoData );
  r[static_cast<size_t>( 2 ) * 5 + 2] = 3; // lone class inside NoData
  const auto out = ClassificationPostProcessor::applySieveFilter( r, 5, 5, 2, 4 );
  REQUIRE( out[static_cast<size_t>( 2 ) * 5 + 2] == kNoData );
  // A large-enough class survives pure NoData surroundings untouched:
  std::vector<int> big( 25, kNoData );
  for ( int y = 1; y <= 3; ++y )
    for ( int x = 1; x <= 3; ++x )
      big[static_cast<size_t>( y ) * 5 + x] = 3; // 9 px >= 2
  const auto kept = ClassificationPostProcessor::applySieveFilter( big, 5, 5, 2, 4 );
  REQUIRE( countClass( kept, 3 ) == 9 );
}

TEST_CASE( "Majority filter removes isolated peaks and keeps ties", "[d15][postprocess]" )
{
  // 5x5 zeros, centre = 7: centre window holds 8 zeros vs 1 seven
  // => strict majority zeroes the peak; all other windows unchanged.
  std::vector<int> r( 25, 0 );
  r[static_cast<size_t>( 2 ) * 5 + 2] = 7;
  MorphologicalFilterConfig cfg;
  cfg.windowSize = 3;
  cfg.noDataValue = kNoData;
  const auto out = ClassificationPostProcessor::applyMajorityFilter( r, 5, 5, cfg );
  REQUIRE( countClass( out, 7 ) == 0 );

  // Centre = 2, neighbours: 4 orthogonal cells of 0 and 4 diagonal corners
  // of 1 => counts {0:4, 1:4, 2:1}; no class holds >4.5 => tie keeps centre.
  std::vector<int> t( 25, 2 );
  t[static_cast<size_t>( 1 ) * 5 + 2] = 0;
  t[static_cast<size_t>( 2 ) * 5 + 1] = 0;
  t[static_cast<size_t>( 2 ) * 5 + 3] = 0;
  t[static_cast<size_t>( 3 ) * 5 + 2] = 0;
  t[static_cast<size_t>( 1 ) * 5 + 1] = 1;
  t[static_cast<size_t>( 1 ) * 5 + 3] = 1;
  t[static_cast<size_t>( 3 ) * 5 + 1] = 1;
  t[static_cast<size_t>( 3 ) * 5 + 3] = 1;
  const auto tie = ClassificationPostProcessor::applyMajorityFilter( t, 5, 5, cfg );
  REQUIRE( tie[static_cast<size_t>( 2 ) * 5 + 2] == 2 );
}

TEST_CASE( "Majority filter ignores NoData neighbours and preserves NoData centres", "[d15][postprocess]" )
{
  MorphologicalFilterConfig cfg;
  cfg.windowSize = 3;
  cfg.noDataValue = kNoData;

  // 3x3: NoData everywhere except centre 0.  Valid window K=1, majority of
  // 1 -> the 0 centre survives (a NoData-counting bug would dissolve it).
  std::vector<int> r( 9, kNoData );
  r[4] = 0;
  const auto out = ClassificationPostProcessor::applyMajorityFilter( r, 3, 3, cfg );
  REQUIRE( out[4] == 0 );

  // NoData centre stays NoData even inside a solid class region.
  std::vector<int> solid( 9, 5 );
  solid[4] = kNoData;
  const auto out2 = ClassificationPostProcessor::applyMajorityFilter( solid, 3, 3, cfg );
  REQUIRE( out2[4] == kNoData );
  REQUIRE( countClass( out2, 5 ) == 8 );
}

TEST_CASE( "Clump-and-eliminate composite matches the sieve guarantee", "[d15][postprocess]" )
{
  const auto in = specRaster10x10();
  const auto out = ClassificationPostProcessor::clumpAndEliminate( in, 10, 10, 4, 8 );
  REQUIRE( countClass( out, 1 ) == 16 );
}

TEST_CASE( "Degenerate inputs round-trip or return empty", "[d15][postprocess]" )
{
  MorphologicalFilterConfig cfg;
  REQUIRE( ClassificationPostProcessor::applyMajorityFilter( {}, 0, 0, cfg ).empty() );
  REQUIRE( ClassificationPostProcessor::applySieveFilter( {}, 0, 0, 4, 8 ).empty() );

  const auto in = specRaster10x10();
  // minPixelSize <= 1 keeps every component: output == input.
  const auto kept = ClassificationPostProcessor::applySieveFilter( in, 10, 10, 1, 8 );
  REQUIRE( kept == in );

  // Size mismatch is refused rather than read out of bounds.
  REQUIRE( ClassificationPostProcessor::applySieveFilter( in, 7, 7, 4, 8 ).empty() );
}
