// src/operators/runtime/detection_postprocess.cpp
#include "detection_postprocess.h"

#include "operators/framework/rs_operator_error.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_map>

namespace sicnu::operators::runtime {

namespace {

/// IoU of two raster-pixel boxes (corner + size form).
double iou( const DetectionBox &a, const DetectionBox &b )
{
  const double ax1 = a.x, ay1 = a.y, ax2 = a.x + a.w, ay2 = a.y + a.h;
  const double bx1 = b.x, by1 = b.y, bx2 = b.x + b.w, by2 = b.y + b.h;
  const double interW = std::max( 0.0, std::min( ax2, bx2 ) - std::max( ax1, bx1 ) );
  const double interH = std::max( 0.0, std::min( ay2, by2 ) - std::max( ay1, by1 ) );
  const double inter = interW * interH;
  const double areaA = std::max( 0.0, ax2 - ax1 ) * std::max( 0.0, ay2 - ay1 );
  const double areaB = std::max( 0.0, bx2 - bx1 ) * std::max( 0.0, by2 - by1 );
  const double unionArea = areaA + areaB - inter;
  return unionArea > 0.0 ? inter / unionArea : 0.0;
}

/// Channels one candidate occupies under a layout vocabulary.
int channelCountFor( const DetectionDecodeContract &contract, int classCount )
{
  return contract.layout == "xywh_objectness" ? 5 + classCount : 4 + classCount;
}

// --- F-OPS-5: bounded, cancellable greedy NMS ------------------------------
//
// The greedy pass is exact: whether candidate j is suppressed depends only on
// the boxes KEPT before it (every kept box precedes it in the deterministic
// confidence order). IoU > threshold (> 0) requires overlapping rectangles,
// so registering each kept box in every grid cell its rectangle covers — and
// oversized boxes in an always-scanned "large" list — lets a candidate find
// every kept box that could suppress it by scanning only its own covered
// cells plus the large list. The kept set is therefore bit-identical to the
// historical dense O(n²) pass; only the comparison count shrinks.

/// A box registered into more than this many cells is routed to the large
/// list instead (registration cost capped; the large list is always scanned).
constexpr std::size_t kMaxCoveredCells = 64;
/// Cancellation poll granularity inside cell scans (outer loop polls every
/// candidate regardless).
constexpr std::size_t kCancelGranularity = 1024;

std::uint64_t cellKey( int cx, int cy )
{
  return ( static_cast<std::uint64_t>( static_cast<std::uint32_t>( cx ) ) << 32 )
         | static_cast<std::uint32_t>( cy );
}

/// Deterministic cell pitch: twice the median linear box extent (data-derived,
/// never input-order dependent), floored at 1 px.
double cellPitch( const std::vector<DetectionBox> &boxes )
{
  std::vector<float> sizes;
  sizes.reserve( boxes.size() );
  for ( const DetectionBox &b : boxes )
    sizes.push_back( 0.5f * ( b.w + b.h ) );
  const std::size_t mid = sizes.size() / 2;
  std::nth_element( sizes.begin(), sizes.begin() + static_cast<std::ptrdiff_t>( mid ), sizes.end() );
  return std::max( 1.0, 2.0 * static_cast<double>( sizes[mid] ) );
}

struct SpatialGrid
{
    explicit SpatialGrid( double pitch ) : cell( pitch ) {}

    double cell;
    /// Kept-box indices INTO THE ORDERED vector, bucketed by covered cell.
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells;
    /// Kept ordered-indices exempt from cell registration (always scanned).
    std::vector<std::size_t> large;

    int coord( double v ) const { return static_cast<int>( std::floor( v / cell ) ); }

    /// Covered cell ranges; false when the box spans more than
    /// kMaxCoveredCells cells (caller routes it to @p large instead).
    bool covered( const DetectionBox &b, int &x0, int &x1, int &y0, int &y1 ) const
    {
      x0 = coord( b.x );
      x1 = coord( static_cast<double>( b.x ) + b.w );
      y0 = coord( b.y );
      y1 = coord( static_cast<double>( b.y ) + b.h );
      const double spanX = static_cast<double>( x1 ) - x0 + 1.0;
      const double spanY = static_cast<double>( y1 ) - y0 + 1.0;
      return spanX * spanY <= static_cast<double>( kMaxCoveredCells );
    }

    void insert( const DetectionBox &b, std::size_t orderedIndex )
    {
      int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
      if ( !covered( b, x0, x1, y0, y1 ) )
      {
        large.push_back( orderedIndex );
        return;
      }
      for ( int y = y0; y <= y1; ++y )
        for ( int x = x0; x <= x1; ++x )
          cells[cellKey( x, y )].push_back( orderedIndex );
    }
};

/// True when @p candidate is suppressed by any kept box: scans the kept boxes
/// registered in the cells @p candidate covers (every kept box when the
/// candidate spans too many cells to enumerate) plus the large list. @p
/// iouCalls is the shared bounded-granularity cancellation counter.
bool suppressedByKept( const DetectionBox &candidate, const SpatialGrid &grid,
                       const std::vector<DetectionBox> &ordered,
                       const std::vector<std::size_t> &keptOrdered,
                       const std::function<bool()> &cancelled,
                       double iouThreshold,
                       std::size_t &iouCalls )
{
  auto check = [&]( std::size_t keptOrderedIndex ) {
    if ( iou( candidate, ordered[keptOrderedIndex] ) > iouThreshold )
      return true;
    if ( ++iouCalls % kCancelGranularity == 0 && cancelled && cancelled() )
      throw RSOperatorError( ErrorCode::Cancelled,
                             "detection NMS cancelled after " + std::to_string( iouCalls )
                               + " IoU comparisons" );
    return false;
  };

  int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  if ( grid.covered( candidate, x0, x1, y0, y1 ) )
  {
    for ( int y = y0; y <= y1; ++y )
    {
      for ( int x = x0; x <= x1; ++x )
      {
        const auto it = grid.cells.find( cellKey( x, y ) );
        if ( it == grid.cells.end() )
          continue;
        for ( std::size_t idx : it->second )
          if ( check( idx ) )
            return true;
      }
    }
  }
  else
  {
    // Candidate too large to enumerate its cells: only a scan of the KEPT
    // set is exact (suppressed boxes never suppress; future boxes cannot be
    // scanned at all). Bounded by the kept count and cancellation-checked.
    for ( std::size_t idx : keptOrdered )
      if ( check( idx ) )
        return true;
  }
  for ( std::size_t idx : grid.large )
    if ( check( idx ) )
      return true;
  return false;
}

std::vector<DetectionBox> runNonMaxSuppression( const std::vector<DetectionBox> &boxes,
                                                double iouThreshold,
                                                const std::function<bool()> &cancelled )
{
  std::vector<DetectionBox> ordered = boxes;
  std::sort( ordered.begin(), ordered.end(), []( const DetectionBox &a, const DetectionBox &b ) {
    if ( a.confidence != b.confidence )
      return a.confidence > b.confidence;
    // Deterministic tie-break: never depend on input order or addresses.
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

  if ( cancelled && cancelled() )
    throw RSOperatorError( ErrorCode::Cancelled, "detection NMS cancelled before the scan" );

  SpatialGrid grid( cellPitch( ordered ) );
  std::vector<DetectionBox> kept;
  kept.reserve( ordered.size() );
  std::vector<std::size_t> keptOrdered; ///< ordered-indices of kept boxes
  keptOrdered.reserve( ordered.size() );
  std::size_t iouCalls = 0;

  for ( std::size_t i = 0; i < ordered.size(); ++i )
  {
    if ( cancelled && cancelled() )
      throw RSOperatorError( ErrorCode::Cancelled,
                             "detection NMS cancelled at candidate "
                               + std::to_string( i ) + "/" + std::to_string( ordered.size() ) );
    if ( suppressedByKept( ordered[i], grid, ordered, keptOrdered, cancelled, iouThreshold, iouCalls ) )
      continue;
    kept.push_back( ordered[i] );
    keptOrdered.push_back( i );
    grid.insert( ordered[i], i );
  }
  return kept;
}

} // namespace

std::string decodeDetections( const cv::Mat &output, const DetectionDecodeContract &contract,
                              int tileX, int tileY, double scaleX, double scaleY,
                              int rasterW, int rasterH,
                              std::vector<DetectionBox> &out )
{
  if ( output.empty() || output.dims != 3 )
    return "detection head output must be a 3-D tensor (1, C, N) or (1, N, C), got dims="
             + std::to_string( output.dims );
  if ( output.type() != CV_32F )
    return "detection head output must be float32";

  int C = output.size[1];
  int N = output.size[2];
  bool channelsFirst = true;
  if ( contract.tensorLayout == "channels_last" )
    channelsFirst = false;
  else if ( contract.tensorLayout == "auto" )
  {
    // Deterministic heuristic: the class axis is the SMALL one (a detection
    // head has 5..~100 channels vs hundreds..thousands of candidates).
    channelsFirst = C <= N;
  }
  if ( !channelsFirst )
    std::swap( C, N );

  const bool withObjectness = contract.layout == "xywh_objectness";
  const int classCount = static_cast<int>( contract.classes.size() );
  const int expectedChannels = channelCountFor( contract, classCount );
  if ( classCount == 0 )
    return "detection contracts must declare output.detection.classes (the decode maps "
           "class channels to names)";
  if ( C != expectedChannels )
    return "detection head has " + std::to_string( C ) + " channels but the contract expects "
             + std::to_string( expectedChannels ) + " (layout '" + contract.layout + "' with "
             + std::to_string( classCount ) + " classes)";

  const float *data = output.ptr<float>( 0 );
  auto at = [&]( int channel, int candidate ) {
    return channelsFirst ? data[static_cast<std::size_t>( channel ) * N + candidate]
                         : data[static_cast<std::size_t>( candidate ) * C + channel];
  };

  for ( int n = 0; n < N; ++n )
  {
    // 1) class scores (and objectness gate).
    const float objectness = withObjectness ? at( 4, n ) : 1.0f;
    int bestClass = 0;
    float bestScore = -std::numeric_limits<float>::infinity();
    for ( int c = 0; c < classCount; ++c )
    {
      const float score = at( ( withObjectness ? 5 : 4 ) + c, n );
      if ( score > bestScore )
      {
        bestScore = score;
        bestClass = c;
      }
    }
    const float confidence = withObjectness ? objectness * bestScore : bestScore;
    if ( !( confidence >= contract.confThreshold ) ) // NaN-safe gate
      continue;

    // 2) center/size (fed pixels) → corner/size (raster pixels). The tile
    // origin maps fed pixel (0,0); fedPx * scale = rasterPx because resize
    // to_input scaled the core+halo window uniformly.
    const float cx = at( 0, n );
    const float cy = at( 1, n );
    const float w = at( 2, n );
    const float h = at( 3, n );
    DetectionBox box;
    box.x = static_cast<float>( tileX + ( cx - w * 0.5f ) * scaleX );
    box.y = static_cast<float>( tileY + ( cy - h * 0.5f ) * scaleY );
    box.w = static_cast<float>( w * scaleX );
    box.h = static_cast<float>( h * scaleY );
    box.classId = bestClass;
    box.confidence = confidence;

    // 3) clamp into the raster; degenerate boxes (outside or empty) drop.
    const float x1 = std::clamp( box.x, 0.0f, static_cast<float>( rasterW ) );
    const float y1 = std::clamp( box.y, 0.0f, static_cast<float>( rasterH ) );
    const float x2 = std::clamp( box.x + box.w, 0.0f, static_cast<float>( rasterW ) );
    const float y2 = std::clamp( box.y + box.h, 0.0f, static_cast<float>( rasterH ) );
    box.x = x1;
    box.y = y1;
    box.w = x2 - x1;
    box.h = y2 - y1;
    if ( box.w <= 0.0f || box.h <= 0.0f )
      continue;

    out.push_back( box );
  }
  return {};
}

std::vector<DetectionBox> nonMaxSuppression( const std::vector<DetectionBox> &boxes,
                                             double iouThreshold )
{
  return runNonMaxSuppression( boxes, iouThreshold, {} );
}

std::vector<DetectionBox> nonMaxSuppression( const std::vector<DetectionBox> &boxes,
                                             double iouThreshold,
                                             const std::function<bool()> &cancelled )
{
  return runNonMaxSuppression( boxes, iouThreshold, cancelled );
}

void dedupDetections( std::vector<DetectionBox> &boxes, double iouThreshold )
{
  dedupDetections( boxes, iouThreshold, {} );
}

void dedupDetections( std::vector<DetectionBox> &boxes, double iouThreshold,
                      const std::function<bool()> &cancelled )
{
  // Exact-duplicate collapse: overlap seams re-detect the identical object
  // with bit-equal geometry (same tile math); a set keyed on the geometry
  // removes those even below the NMS threshold... NMS already removes them
  // (IoU 1 > threshold), so the collapse is an O(n) pre-pass that keeps the
  // NMS input smaller; correctness does not depend on it.
  std::set<std::tuple<int, int, int, int, int, float>> seen;
  std::vector<DetectionBox> collapsed;
  collapsed.reserve( boxes.size() );
  std::size_t scan = 0;
  for ( const DetectionBox &b : boxes )
  {
    if ( cancelled && ( ++scan % kCancelGranularity == 0 ) && cancelled() )
      throw RSOperatorError( ErrorCode::Cancelled,
                             "detection dedup cancelled after scanning "
                               + std::to_string( scan ) + " boxes" );
    const auto key = std::make_tuple( static_cast<int>( std::lround( b.x * 100.0f ) ),
                                      static_cast<int>( std::lround( b.y * 100.0f ) ),
                                      static_cast<int>( std::lround( b.w * 100.0f ) ),
                                      static_cast<int>( std::lround( b.h * 100.0f ) ),
                                      b.classId, b.confidence );
    if ( seen.insert( key ).second )
      collapsed.push_back( b );
  }
  boxes = runNonMaxSuppression( collapsed, iouThreshold, cancelled );
}

} // namespace sicnu::operators::runtime
