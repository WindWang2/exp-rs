// tests/test_r14_B_detection_decode.cpp — R14-B detection decode/postprocess
// regressions: NaN geometry gating, batch-dimension refusal, ambiguous "auto"
// layout, NaN-hardened public NMS, negative classId clamp in the shared
// vector writer, and the TensorBlob fromMat/isValid contracts.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/runtime/detection_postprocess.h"
#include "operators/runtime/detection_tile_engine.h"
#include "operators/runtime/tensor_blob.h"
#include "operators/framework/rs_operator_error.h"

#include <opencv2/core.hpp>

#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using sicnu::operators::runtime::DetectionBox;
using sicnu::operators::runtime::DetectionDecodeContract;
using sicnu::operators::runtime::TensorBlob;
using sicnu::operators::runtime::TensorDType;
using sicnu::operators::runtime::decodeDetections;
using sicnu::operators::runtime::dedupDetections;
using sicnu::operators::runtime::nonMaxSuppression;
using sicnu::operators::runtime::writeDetectionVector;

DetectionDecodeContract twoClassContract()
{
  DetectionDecodeContract contract;
  contract.layout = "xywh_conf";
  contract.classes = { "bg", "fg" };
  contract.confThreshold = 0.25;
  contract.nmsIou = 0.5;
  contract.tensorLayout = "channels_first";
  return contract;
}

/// (1, C, N) channels-first float32 head tensor.
cv::Mat head( const std::vector<std::vector<float>> &channels, int n )
{
  const int c = static_cast<int>( channels.size() );
  cv::Mat mat( 3, std::vector<int>{ 1, c, n }.data(), CV_32F );
  for ( int ci = 0; ci < c; ++ci )
    for ( int ni = 0; ni < n; ++ni )
      mat.ptr<float>( 0 )[static_cast<std::size_t>( ci ) * n + ni] = channels[static_cast<std::size_t>( ci )][static_cast<std::size_t>( ni )];
  return mat;
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

} // namespace

// ---------------------------------------------------------------------------
// decodeDetections
// ---------------------------------------------------------------------------

TEST_CASE( "#r14B decode drops NaN geometry instead of shipping NaN boxes",
           "[models][detection][r14B]" )
{
  const DetectionDecodeContract contract = twoClassContract();
  // layout xywh_conf: C = 4 + 2 classes = 6; candidates: [finite, NaN w, NaN h].
  const std::vector<std::vector<float>> chans = { { 10, 10, 10 }, { 10, 10, 10 },
                                                  { 5, kNaN, 5 }, { 5, 5, kNaN },
                                                  { 0.1, 0.1, 0.1 }, { 0.9, 0.9, 0.9 } };
  const cv::Mat tensor = head( chans, 3 );

  std::vector<DetectionBox> boxes;
  const std::string error = decodeDetections( tensor, contract, 0, 0, 1.0, 1.0, 64, 64, boxes );
  REQUIRE( error.empty() );
  REQUIRE( boxes.size() == 1 ); // only the all-finite candidate survives
  CHECK( std::isfinite( boxes[0].x ) );
  CHECK( std::isfinite( boxes[0].y ) );
  CHECK( std::isfinite( boxes[0].w ) );
  CHECK( std::isfinite( boxes[0].h ) );
  CHECK( boxes[0].classId == 1 );
}

TEST_CASE( "#r14B decode refuses a batched head tensor instead of decoding sample 0",
           "[models][detection][r14B]" )
{
  const DetectionDecodeContract contract = twoClassContract();
  std::vector<std::vector<float>> chans( 6, std::vector<float>( 4, 1.0f ) );
  chans[0] = { 10, 10, 10, 10 };
  cv::Mat batched( 3, std::vector<int>{ 2, 6, 2 }.data(), CV_32F );
  for ( int ci = 0; ci < 6; ++ci )
    for ( int ni = 0; ni < 4; ++ni )
      batched.ptr<float>( 0 )[static_cast<std::size_t>( ci ) * 4 + ni] = chans[static_cast<std::size_t>( ci )][static_cast<std::size_t>( ni )];

  std::vector<DetectionBox> boxes;
  const std::string error = decodeDetections( batched, contract, 0, 0, 1.0, 1.0, 64, 64, boxes );
  CHECK( !error.empty() );
  CHECK( error.find( "batch" ) != std::string::npos );
  CHECK( boxes.empty() );
}

TEST_CASE( "#r14B decode refuses ambiguous auto layout when C == N",
           "[models][detection][r14B]" )
{
  DetectionDecodeContract contract = twoClassContract();
  contract.tensorLayout = "auto";
  // 6 channels, 6 candidates: both axes read as "the small one".
  const cv::Mat square = head( { { 10, 10, 10, 10, 10, 10 },
                                 { 10, 10, 10, 10, 10, 10 },
                                 { 5, 5, 5, 5, 5, 5 },
                                 { 5, 5, 5, 5, 5, 5 },
                                 { 0.1, 0.1, 0.1, 0.1, 0.1, 0.1 },
                                 { 0.9, 0.9, 0.9, 0.9, 0.9, 0.9 } },
                               6 );
  std::vector<DetectionBox> boxes;
  const std::string error = decodeDetections( square, contract, 0, 0, 1.0, 1.0, 64, 64, boxes );
  CHECK( !error.empty() );
  CHECK( error.find( "ambiguous" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// nonMaxSuppression (public API surface)
// ---------------------------------------------------------------------------

TEST_CASE( "#r14B public NMS tolerates NaN boxes without sort UB",
           "[models][detection][r14B]" )
{
  std::vector<DetectionBox> boxes;
  for ( int i = 0; i < 64; ++i )
  {
    DetectionBox b;
    b.x = static_cast<float>( ( i % 8 ) * 10 );
    b.y = static_cast<float>( ( i / 8 ) * 10 );
    b.w = 4.0f;
    b.h = 4.0f;
    b.classId = 0;
    b.confidence = 0.9f - i * 0.001f;
    boxes.push_back( b );
  }
  DetectionBox nan1 = boxes.front();
  nan1.w = kNaN;
  nan1.confidence = 1.0f; // sorts first
  DetectionBox nan2 = boxes.back();
  nan2.h = kNaN;
  nan2.confidence = 0.99f;
  boxes.push_back( nan1 );
  boxes.push_back( nan2 );

  const std::vector<DetectionBox> kept = nonMaxSuppression( boxes, 0.5 );
  // Every kept box must be finite — NaN geometry is un-suppressible garbage
  // (IoU collapses to 0) that previously escaped into published vectors.
  for ( const DetectionBox &b : kept )
  {
    INFO( "kept box at (" << b.x << "," << b.y << ")" );
    CHECK( std::isfinite( b.x ) );
    CHECK( std::isfinite( b.y ) );
    CHECK( std::isfinite( b.w ) );
    CHECK( std::isfinite( b.h ) );
  }
  CHECK( kept.size() == 64 );
}

// ---------------------------------------------------------------------------
// writeDetectionVector negative classId
// ---------------------------------------------------------------------------

TEST_CASE( "#r14B writeDetectionVector clamps a negative classId, never reads OOB",
           "[models][detection][r14B]" )
{
  QTemporaryDir dir;
  const std::string out = dir.filePath( QStringLiteral( "boxes.gpkg" ) ).toStdString();
  std::vector<DetectionBox> boxes;
  DetectionBox bad;
  bad.x = 1;
  bad.y = 1;
  bad.w = 4;
  bad.h = 4;
  bad.classId = -3; // a stray negative: (size_t)-3 is a huge OOB index
  bad.confidence = 0.9f;
  boxes.push_back( bad );

  REQUIRE_NOTHROW( writeDetectionVector( boxes, { "a", "b" },
                                         { 0, 1, 0, 0, 1, -1 }, QString(), out ) );
  CHECK( QFileInfo::exists( QString::fromStdString( out ) ) );
}

// ---------------------------------------------------------------------------
// TensorBlob
// ---------------------------------------------------------------------------

TEST_CASE( "#r14B TensorBlob::fromMat keeps the channel axis of 2-D multi-channel mats",
           "[models][tensor][r14B]" )
{
  cv::Mat hwc( 4, 6, CV_8UC3 );
  for ( int r = 0; r < 4; ++r )
    for ( int c = 0; c < 6; ++c )
      for ( int ch = 0; ch < 3; ++ch )
        hwc.ptr<cv::Vec3b>( r )[c][static_cast<size_t>( ch )] =
          static_cast<uchar>( r * 100 + c * 10 + ch );

  const TensorBlob blob = TensorBlob::fromMat( hwc );
  CHECK( blob.isValid() ); // was rank {4,6} with 432 bytes — always invalid
  CHECK( blob.rank() == 3 );
  CHECK( blob.shape == std::vector<std::int64_t>( { 4, 6, 3 } ) );
  CHECK( blob.dtype == TensorDType::UInt8 );
  CHECK( blob.bytes.size() == 72 );
  CHECK( blob.bytes[static_cast<std::size_t>( 0 ) * 3 + 2] == 2 );
}

TEST_CASE( "#r14B TensorBlob::isValid refuses overflowing shape products",
           "[models][tensor][r14B]" )
{
  TensorBlob hostile;
  hostile.dtype = TensorDType::Float32;
  // 2^32 * 2^32 * 4 wraps size_t arithmetic to 0 — matching an empty payload.
  hostile.shape = { 4294967296LL, 4294967296LL, 4 };
  hostile.bytes = {};
  CHECK_FALSE( hostile.isValid() );
  CHECK( hostile.countOrZero() == 0 );
}
