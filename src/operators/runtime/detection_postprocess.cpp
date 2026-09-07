// src/operators/runtime/detection_postprocess.cpp
#include "detection_postprocess.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

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

void dedupDetections( std::vector<DetectionBox> &boxes, double iouThreshold )
{
  // Exact-duplicate collapse: overlap seams re-detect the identical object
  // with bit-equal geometry (same tile math); a set keyed on the geometry
  // removes those even below the NMS threshold... NMS already removes them
  // (IoU 1 > threshold), so the collapse is an O(n) pre-pass that keeps the
  // NMS input smaller; correctness does not depend on it.
  std::set<std::tuple<int, int, int, int, int, float>> seen;
  std::vector<DetectionBox> collapsed;
  collapsed.reserve( boxes.size() );
  for ( const DetectionBox &b : boxes )
  {
    const auto key = std::make_tuple( static_cast<int>( std::lround( b.x * 100.0f ) ),
                                      static_cast<int>( std::lround( b.y * 100.0f ) ),
                                      static_cast<int>( std::lround( b.w * 100.0f ) ),
                                      static_cast<int>( std::lround( b.h * 100.0f ) ),
                                      b.classId, b.confidence );
    if ( seen.insert( key ).second )
      collapsed.push_back( b );
  }
  boxes = nonMaxSuppression( collapsed, iouThreshold );
}

} // namespace sicnu::operators::runtime
