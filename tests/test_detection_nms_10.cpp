// tests/test_detection_nms_10.cpp — F-OPS-5 regression: the whole-raster
// detection NMS/dedup pass must be grid-bounded (never a dense O(n²) scan
// over the max_detections budget) and must honour the operator cancel
// predicate. Equivalence is proven against a test-local dense reference NMS
// (the exact historical semantics); cancellation is proven by a predicate
// that flips mid-pass and MUST abort with ErrorCode::Cancelled.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/detection_postprocess.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace {

using sicnu::operators::RSOperatorError;
using sicnu::operators::runtime::DetectionBox;
using sicnu::operators::runtime::dedupDetections;
using sicnu::operators::runtime::nonMaxSuppression;

DetectionBox box( float x, float y, float w, float h, int classId, float conf )
{
  DetectionBox b;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  b.classId = classId;
  b.confidence = conf;
  return b;
}

/// Test-local DENSE reference NMS — the historical O(n²) semantics the grid
/// pass must reproduce bit-identically (same deterministic order, same kept set).
std::vector<DetectionBox> denseReferenceNms( const std::vector<DetectionBox> &boxes, double iouThreshold )
{
  std::vector<DetectionBox> ordered = boxes;
  std::sort( ordered.begin(), ordered.end(), []( const DetectionBox &a, const DetectionBox &b ) {
    if ( a.confidence != b.confidence )
      return a.confidence > b.confidence;
    if ( a.classId != b.classId )
      return a.classId < b.classId;
    if ( a.x != b.x )
      return a.x < b.x;
    if ( a.y != b.y )
      return a.y < b.y;
    if ( a.w != b.w )
      return a.w < b.w;
    return a.h < b.h;
  } );
  auto iou = []( const DetectionBox &a, const DetectionBox &b ) {
    const double ax1 = a.x, ay1 = a.y, ax2 = a.x + a.w, ay2 = a.y + a.h;
    const double bx1 = b.x, by1 = b.y, bx2 = b.x + b.w, by2 = b.y + b.h;
    const double interW = std::max( 0.0, std::min( ax2, bx2 ) - std::max( ax1, bx1 ) );
    const double interH = std::max( 0.0, std::min( ay2, by2 ) - std::max( ay1, by1 ) );
    const double inter = interW * interH;
    const double areaA = std::max( 0.0, ax2 - ax1 ) * std::max( 0.0, ay2 - ay1 );
    const double areaB = std::max( 0.0, bx2 - bx1 ) * std::max( 0.0, by2 - by1 );
    const double unionArea = areaA + areaB - inter;
    return unionArea > 0.0 ? inter / unionArea : 0.0;
  };
  std::vector<DetectionBox> kept;
  std::vector<bool> suppressed( ordered.size(), false );
  for ( std::size_t i = 0; i < ordered.size(); ++i )
  {
    if ( suppressed[i] )
      continue;
    kept.push_back( ordered[i] );
    for ( std::size_t j = i + 1; j < ordered.size(); ++j )
    {
      if ( suppressed[j] )
        continue;
      if ( iou( ordered[i], ordered[j] ) > iouThreshold )
        suppressed[j] = true;
    }
  }
  return kept;
}

/// Deterministic LCG box field: clustered, overlapping, mixed sizes.
std::vector<DetectionBox> syntheticField( std::size_t count, unsigned seed, float spread,
                                          float maxSize )
{
  std::vector<DetectionBox> boxes;
  boxes.reserve( count );
  unsigned state = seed;
  auto next = [&state]() {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>( state >> 8 ) / static_cast<float>( 1u << 24 );
  };
  for ( std::size_t i = 0; i < count; ++i )
  {
    const float w = 2.0f + next() * maxSize;
    const float h = 2.0f + next() * maxSize;
    boxes.push_back( box( next() * spread, next() * spread, w, h,
                          static_cast<int>( next() * 3.0f ), 0.05f + next() * 0.9f ) );
  }
  return boxes;
}

bool sameKeptSet( const std::vector<DetectionBox> &a, const std::vector<DetectionBox> &b )
{
  if ( a.size() != b.size() )
    return false;
  for ( std::size_t i = 0; i < a.size(); ++i )
  {
    if ( a[i].classId != b[i].classId || a[i].confidence != b[i].confidence )
      return false;
    if ( a[i].x != b[i].x || a[i].y != b[i].y || a[i].w != b[i].w || a[i].h != b[i].h )
      return false;
  }
  return true;
}

} // namespace

TEST_CASE( "F-OPS-5: grid NMS reproduces the dense reference kept set exactly",
           "[detection][nms][fops5]" )
{
  const double iouThreshold = 0.45;
  // Clustered overlapping fields with a few dominant oversized boxes mixed in
  // (they exercise the large-list path and the un-enumerable-candidate path).
  auto field = syntheticField( 2000, 20260914u, 4000.0f, 120.0f );
  field.push_back( box( 0.0f, 0.0f, 4000.0f, 4000.0f, 0, 0.99f ) );
  field.push_back( box( 500.0f, 500.0f, 3000.0f, 2000.0f, 1, 0.98f ) );

  const auto reference = denseReferenceNms( field, iouThreshold );
  const auto gridKept = nonMaxSuppression( field, iouThreshold );
  REQUIRE( sameKeptSet( reference, gridKept ) );

  // Sub-pixel/degenerate-density field: pitch floors at 1 px, tiny boxes.
  auto tiny = syntheticField( 500, 7u, 8.0f, 1.5f );
  REQUIRE( sameKeptSet( denseReferenceNms( tiny, iouThreshold ),
                        nonMaxSuppression( tiny, iouThreshold ) ) );

  // All-overlapping single cluster: dense suppression chains behave identically.
  std::vector<DetectionBox> cluster;
  for ( int i = 0; i < 200; ++i )
    cluster.push_back( box( static_cast<float>( i ), static_cast<float>( i ), 100.0f, 100.0f,
                            i % 3, 1.0f - i * 0.001f ) );
  REQUIRE( sameKeptSet( denseReferenceNms( cluster, 0.3 ),
                        nonMaxSuppression( cluster, 0.3 ) ) );
}

TEST_CASE( "F-OPS-5: dedup at the max_detections budget completes bounded (100k boxes)",
           "[detection][nms][fops5]" )
{
  // 100,000 mutually disjoint boxes — the finding's reproduction shape. The
  // dense pass needs ~5×10⁹ IoU comparisons here; the grid pass a handful per
  // candidate. No wall-clock gate (host-load fragile): the contract asserted
  // is completion plus exact output (all boxes kept, deterministic order).
  constexpr std::size_t kCount = 100000;
  std::vector<DetectionBox> boxes;
  boxes.reserve( kCount );
  for ( std::size_t i = 0; i < kCount; ++i )
  {
    const float x = static_cast<float>( ( i % 1000 ) * 400 );
    const float y = static_cast<float>( ( i / 1000 ) * 400 );
    boxes.push_back( box( x, y, 100.0f, 100.0f, 0, 0.5f ) );
  }
  const auto started = std::chrono::steady_clock::now();
  dedupDetections( boxes, 0.45 );
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started )
                           .count();
  REQUIRE( boxes.size() == kCount );
  // Generous load-independent guard: if the grid regressed to a dense scan,
  // this 10 s ceiling blows past the minute mark on the same hardware.
  REQUIRE( elapsedMs < 10000 );
}

TEST_CASE( "F-OPS-5: NMS honours a mid-pass cancel predicate with a typed Cancelled error",
           "[detection][nms][fops5][cancel]" )
{
  auto field = syntheticField( 50000, 99u, 100000.0f, 60.0f );

  std::atomic<int> polls { 0 };
  bool flipped = false;
  bool cancelled = false;
  const std::function<bool()> predicate = [&]() {
    const int n = ++polls;
    if ( !flipped && n >= 8 )
    {
      flipped = true;
      cancelled = true;
    }
    return cancelled;
  };

  REQUIRE_THROWS_AS( nonMaxSuppression( field, 0.45, predicate ), RSOperatorError );
  REQUIRE( polls.load() >= 8 );
  try
  {
    nonMaxSuppression( field, 0.45, predicate );
    FAIL( "expected RSOperatorError" );
  }
  catch ( const RSOperatorError &e )
  {
    REQUIRE( e.code() == sicnu::operators::ErrorCode::Cancelled );
  }
}

TEST_CASE( "F-OPS-5: dedup cancel overload keeps the exact-duplicate collapse contract",
           "[detection][nms][fops5][cancel]" )
{
  std::vector<DetectionBox> boxes;
  // Bit-equal duplicates from overlap seams collapse in the O(n) pre-pass…
  boxes.push_back( box( 10.0f, 10.0f, 20.0f, 20.0f, 0, 0.9f ) );
  boxes.push_back( box( 10.0f, 10.0f, 20.0f, 20.0f, 0, 0.9f ) );
  boxes.push_back( box( 10.0f, 10.0f, 20.0f, 20.0f, 0, 0.9f ) );
  // …plus two IoU-overlapping boxes where the NMS phase does the dedup.
  boxes.push_back( box( 50.0f, 50.0f, 40.0f, 40.0f, 1, 0.8f ) );
  boxes.push_back( box( 55.0f, 55.0f, 40.0f, 40.0f, 1, 0.7f ) );

  dedupDetections( boxes, 0.45, [] { return false; } );
  REQUIRE( boxes.size() == 2 );
  REQUIRE( boxes[0].confidence == 0.9f );
  REQUIRE( boxes[1].confidence == 0.8f );

  std::vector<DetectionBox> cancelMe = {
    box( 0.0f, 0.0f, 10.0f, 10.0f, 0, 0.5f ),
  };
  int calls = 0;
  REQUIRE_THROWS_AS( dedupDetections( cancelMe, 0.45,
                                      [&] { return ++calls >= 1; } ),
                     RSOperatorError );
}
