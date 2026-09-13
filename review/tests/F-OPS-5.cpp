// F-OPS-5 — whole-raster NMS is O(n^2) with no cancellation injection point.
// Review draft, not built. dedupDetections is a pure function: no model needed.
#include <catch2/catch.hpp>
#include "operators/runtime/detection_postprocess.h"
#include <chrono>

TEST_CASE( "F-OPS-5: NMS at the maxDetections budget must stay interactive or cancellable",
           "[review][F-OPS-5][test-draft]" )
{
  std::vector<sicnu::operators::runtime::DetectionBox> boxes;
  boxes.reserve( 100000 );
  for ( int i = 0; i < 100000; ++i )
  {
    const float x = static_cast<float>( i % 1000 ) * 10.0f;
    const float y = static_cast<float>( i / 1000 ) * 10.0f;
    boxes.push_back( { x, y, 5.0f, 5.0f, i % 8, 0.9f } ); // non-overlapping: NMS keeps all
  }
  const auto start = std::chrono::steady_clock::now();
  sicnu::operators::runtime::dedupDetections( boxes, 0.45 );
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start ).count();
  // Current master: ~5e9 IoU comparisons, tens of seconds single-threaded,
  // no cancellation parameter exists — CHECK fails on the time bound.
  CHECK( elapsedMs < 1000 );
}
