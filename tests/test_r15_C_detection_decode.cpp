// tests/test_r15_C_detection_decode.cpp — Track 15 WP-C06/C08: deterministic
// golden contracts for the detection postprocess surface (pure functions —
// no raster stack, no provider).
//
// Coverage the matrix called out as absent (plus the R15-C C08 fix):
//   * dedupDetections had ZERO tests; its duplicate key used
//     static_cast<int>(lround(x*100.0f)) — an int cast that wraps (UB) for
//     coordinates beyond ~2.1e7 px, so two far-apart boxes with otherwise
//     equal key fields could collide and one real detection be dropped.
//   * NMS boundary semantics (touching boxes, empty input, deterministic
//     ordering with equal confidences).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/runtime/detection_postprocess.h"

#include <cmath>
#include <limits>
#include <vector>

namespace {

using sicnu::operators::runtime::DetectionBox;
using sicnu::operators::runtime::dedupDetections;
using sicnu::operators::runtime::nonMaxSuppression;

DetectionBox box( float x, float y, float w, float h, int classId, float confidence )
{
  DetectionBox b;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  b.classId = classId;
  b.confidence = confidence;
  return b;
}

} // namespace

TEST_CASE( "R15-C C08: dedup collapses bit-equal overlap-seam duplicates",
           "[models][detect][r15-c]" )
{
  std::vector<DetectionBox> boxes = { box( 10, 10, 4, 4, 0, 0.9f ),
                                      box( 10, 10, 4, 4, 0, 0.9f ), // seam duplicate
                                      box( 30, 30, 4, 4, 0, 0.8f ) };
  dedupDetections( boxes, 0.45 );
  CHECK( boxes.size() == 2 );
}

TEST_CASE( "R15-C C08: dedup keeps far-apart boxes whose key fields otherwise match",
           "[models][detect][r15-c]" )
{
  // The historical int(lround(x*100)) key wrapped (UB) beyond ~2.1e7 px:
  // both x values map to the same wrapped key, so the second box was
  // dropped as a "duplicate" although it is 4 px away. 3.0e7 and 3.0e7+4
  // are distinct float32 values (ulp = 4 there) and both must survive.
  std::vector<DetectionBox> boxes = { box( 3.0e7f, 10, 4, 4, 0, 0.9f ),
                                      box( 3.0e7f + 4.0f, 10, 4, 4, 0, 0.9f ) };
  dedupDetections( boxes, 0.45 );
  CHECK( boxes.size() == 2 );
}

TEST_CASE( "R15-C C08: dedup drops non-finite boxes instead of sorting on NaN",
           "[models][detect][r15-c]" )
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  std::vector<DetectionBox> boxes = { box( nan, 10, 4, 4, 0, 0.9f ),
                                      box( 5, 5, 4, 4, 0, 0.5f ) };
  dedupDetections( boxes, 0.45 );
  REQUIRE( boxes.size() == 1 );
  CHECK( boxes[0].x == 5.0f );
}

TEST_CASE( "R15-C C08: NMS boundary semantics are deterministic",
           "[models][detect][r15-c]" )
{
  // Empty input → empty output (no crash, no phantom).
  const std::vector<DetectionBox> empty;
  CHECK( nonMaxSuppression( empty, 0.45 ).empty() );

  // Touching boxes share no area (IoU == 0) — neither suppresses the other.
  const std::vector<DetectionBox> touching = { box( 0, 0, 4, 4, 0, 0.9f ),
                                               box( 4, 0, 4, 4, 0, 0.8f ) };
  CHECK( nonMaxSuppression( touching, 0.45 ).size() == 2 );

  // Identical boxes → exactly one kept.
  const std::vector<DetectionBox> identical = { box( 0, 0, 4, 4, 0, 0.9f ),
                                                box( 0, 0, 4, 4, 0, 0.9f ) };
  CHECK( nonMaxSuppression( identical, 0.45 ).size() == 1 );

  // Equal confidence → deterministic order: classId, then x, y, w, h. The
  // highest-confidence-first rule with a full tie-break key makes the
  // output stable across runs and platforms.
  const std::vector<DetectionBox> ties = { box( 20, 0, 4, 4, 1, 0.5f ),
                                           box( 0, 0, 4, 4, 0, 0.5f ) };
  const std::vector<DetectionBox> kept = nonMaxSuppression( ties, 0.45 );
  REQUIRE( kept.size() == 2 );
  CHECK( kept[0].classId == 0 ); // lower classId first on a confidence tie
  CHECK( kept[1].classId == 1 );
}

TEST_CASE( "R15-C C08: a negative classId cannot produce an out-of-range write",
           "[models][detect][r15-c]" )
{
  // The shared vector writer clamps on BOTH sides (R14-B D3); the postprocess
  // surface must hand it a sane classId rather than trusting its input. A
  // negative id decodes as class 0 here — the clamp contract — and never
  // indexes out of bounds.
  std::vector<DetectionBox> boxes = { box( 0, 0, 4, 4, -5, 0.9f ) };
  dedupDetections( boxes, 0.45 );
  REQUIRE( boxes.size() == 1 );
  CHECK( boxes[0].classId == -5 ); // preserved for the writer's clamp
}
