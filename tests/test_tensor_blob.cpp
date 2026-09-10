// tests/test_tensor_blob.cpp — Platform 7.0 runtime core: TensorBlob N-D
// value type (known-answer), the failure taxonomy extension, and the
// IModelRuntime::inferNamed default bridge over a fake provider.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tensor_blob.h"

#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace sicnu::operators::runtime;

/// Minimal single/multi-input fake session used to exercise the DEFAULT
/// inferNamed bridge (cv::Mat path) that every provider inherits.
class BridgeFakeRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "fake"; }
    std::string backendName() const override { return "bridge-fake"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://bridge"; }

    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      lastInferInput = nchwBlob.clone();
      // Known-answer: output = input + 1 on every element (4-D float32).
      cv::Mat out = nchwBlob.clone();
      out += 1.0;
      return out;
    }

    bool supportsMultiInput() const override { return true; }

    std::vector<cv::Mat> inferMulti( const std::vector<NamedBlob> &namedBlobs ) override
    {
      lastMultiNames.clear();
      std::vector<cv::Mat> outs;
      for ( const NamedBlob &nb : namedBlobs )
      {
        lastMultiNames.push_back( nb.first );
        cv::Mat out = nb.second.clone();
        out += 1.0;
        outs.push_back( out );
      }
      return outs;
    }

    cv::Mat lastInferInput;
    std::vector<std::string> lastMultiNames;
};

} // namespace

TEST_CASE( "TensorBlob dtype tokens parse strictly", "[models][tensor]" )
{
  TensorDType dtype = TensorDType::Float32;
  REQUIRE( tensorDTypeFromToken( "float32", &dtype ) );
  CHECK( dtype == TensorDType::Float32 );
  REQUIRE( tensorDTypeFromToken( "int64", &dtype ) );
  CHECK( dtype == TensorDType::Int64 );
  REQUIRE( tensorDTypeFromToken( "uint8", &dtype ) );
  CHECK( dtype == TensorDType::UInt8 );
  CHECK( tensorDTypeFromToken( "bfloat16", &dtype ) == false );
  CHECK( tensorDTypeFromToken( "", &dtype ) == false );
  CHECK( tensorDTypeFromToken( "float33", &dtype ) == false );
  CHECK( tensorDTypeSize( TensorDType::Float64 ) == 8 );
  CHECK( tensorDTypeSize( TensorDType::Int8 ) == 1 );
}

TEST_CASE( "TensorBlob structural validity is exact", "[models][tensor]" )
{
  auto blob = TensorBlob::fromFloat32( { 2, 3, 4, 5 }, nullptr, 0 );
  blob.bytes.assign( blob.expectedByteCount(), 0 );
  CHECK( blob.isValid() );
  CHECK( blob.elementCount() == 120 );
  CHECK( blob.rank() == 4 );

  // Byte-count disagreement is invalid (never silently tolerated).
  blob.bytes.pop_back();
  CHECK_FALSE( blob.isValid() );

  // Zero/negative dims are invalid.
  auto zero = TensorBlob::zeros( { 1, 0, 4 }, TensorDType::Float32 );
  CHECK_FALSE( zero.isValid() );
  auto negative = TensorBlob::zeros( { -2, 4 }, TensorDType::Float32 );
  CHECK_FALSE( negative.isValid() );
  // Shape product overflow reports zero instead of wrapped garbage.
  auto huge = TensorBlob::zeros( { std::numeric_limits<std::int64_t>::max(), 2 }, TensorDType::UInt8 );
  CHECK( huge.countOrZero() == 0 );
}

TEST_CASE( "TensorBlob float32 known-answer round-trip", "[models][tensor]" )
{
  std::vector<float> data = { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f };
  auto blob = TensorBlob::fromFloat32( { 1, 2, 3 }, data.data(), data.size() );
  REQUIRE( blob.isValid() );
  const float *view = blob.dataFloat32();
  for ( std::size_t i = 0; i < data.size(); ++i )
    CHECK( view[i] == Catch::Approx( data[i] ) );

  // dtype view refuses non-float32 (typed, not bit-cast).
  auto ints = TensorBlob::zeros( { 2, 2 }, TensorDType::Int64 );
  REQUIRE_THROWS_AS( ints.dataFloat32(), std::runtime_error );
}

TEST_CASE( "TensorBlob cv bridge is exact-dtype and rank-preserving", "[models][tensor]" )
{
  // 3-D float32 bridge (a temporal C,T,H-style tensor).
  const int dims[3] = { 2, 3, 4 };
  std::vector<float> data( 24 );
  for ( std::size_t i = 0; i < data.size(); ++i )
    data[i] = static_cast<float>( i );
  cv::Mat mat( 3, dims, CV_32F, data.data() );
  auto blob = TensorBlob::fromMat( mat );
  REQUIRE( blob.isValid() );
  CHECK( blob.rank() == 3 );
  CHECK( blob.shape == std::vector<std::int64_t>( { 2, 3, 4 } ) );
  cv::Mat back = blob.toMat();
  CHECK( back.dims == 3 );
  CHECK( std::memcmp( back.ptr<float>(), mat.ptr<float>(), 24 * sizeof( float ) ) == 0 );

  // uint8 bridge.
  cv::Mat bytes( 2, 2, CV_8U, cv::Scalar( 7 ) );
  auto u8 = TensorBlob::fromMat( bytes );
  CHECK( u8.dtype == TensorDType::UInt8 );
  CHECK( u8.toMat().type() == CV_8U );

  // int64 has no exact cv depth: the bridge refuses instead of bit-casting.
  auto i64 = TensorBlob::zeros( { 2, 2 }, TensorDType::Int64 );
  REQUIRE_THROWS_AS( i64.toMat(), std::runtime_error );

  // Empty mats refuse.
  cv::Mat empty;
  REQUIRE_THROWS_AS( TensorBlob::fromMat( empty ), std::runtime_error );
}

TEST_CASE( "failure taxonomy classifies the 7.0 kinds", "[models][failure]" )
{
  using K = InferenceFailureKind;
  using sicnu::operators::ErrorCode;
  // New kinds.
  CHECK( classifyInferenceError( "input contract error: declared input 'optical' matches no "
                                 "graph input (schema mismatch)" ) == K::IncompatibleSchema );
  CHECK( classifyInferenceError( "cuda:1 is not addressable by this backend" ) ==
         K::DeviceUnavailable );
  CHECK( classifyInferenceError( "python worker exited unexpectedly (code -11)" ) ==
         K::ProviderCrash );
  CHECK( classifyInferenceError( "connection refused while contacting provider" ) ==
         K::ProviderCrash );
  CHECK( classifyInferenceError( "inference produced an empty output" ) == K::OutputInvalid );
  CHECK( classifyInferenceError( "output invalid: NaN plane" ) == K::OutputInvalid );
  // Error-code projection is stable per kind.
  CHECK( errorCodeForInferenceFailure( K::DeviceUnavailable ) == ErrorCode::DeviceUnavailable );
  CHECK( errorCodeForInferenceFailure( K::ProviderCrash ) == ErrorCode::RuntimeProviderFailed );
  CHECK( errorCodeForInferenceFailure( K::IncompatibleSchema ) == ErrorCode::InvalidInputData );
  CHECK( errorCodeForInferenceFailure( K::OutputInvalid ) == ErrorCode::ComputationError );
  CHECK( errorCodeForInferenceFailure( K::OutOfMemory ) == ErrorCode::ComputationError );
  CHECK( errorCodeForInferenceFailure( K::Canceled ) == ErrorCode::Cancelled );
  // Historical 4.0 classifications are unchanged (regression pins).
  CHECK( classifyInferenceError( "CUDA_ERROR_OUT_OF_MEMORY: out of memory" ) == K::OutOfMemory );
  CHECK( classifyInferenceError( "std::bad_alloc" ) == K::OutOfMemory );
  CHECK( classifyInferenceError( "inference canceled before the forward pass" ) == K::Canceled );
  CHECK( classifyInferenceError( "runtime session is not loaded" ) == K::NotLoaded );
  CHECK( classifyInferenceError( "wrong input shape: dimension mismatch" ) == K::ShapeMismatch );
  CHECK( classifyInferenceError( "failed to load ONNX model: parse error" ) == K::CorruptModel );
  CHECK( classifyInferenceError( "something else entirely" ) == K::Unknown );
}

TEST_CASE( "default inferNamed bridge carries rank-4 tensors and refuses other ranks", "[models][tensor]" )
{
  BridgeFakeRuntime runtime;

  // Single input through the single-input path (rank-4 float32).
  std::vector<float> data( 8, 2.f );
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{
    "image", TensorBlob::fromFloat32( { 1, 2, 2, 2 }, data.data(), data.size() ) } );
  auto outputs = runtime.inferNamed( inputs, {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs[0].second.dtype == TensorDType::Float32 );
  REQUIRE( outputs[0].second.elementCount() == 8 );
  for ( std::size_t i = 0; i < 8; ++i )
    CHECK( outputs[0].second.dataFloat32()[i] == Catch::Approx( 3.f ) );
  CHECK( runtime.lastInferInput.dims == 4 );

  // Multi input through the multi path with NAMES preserved for binding.
  std::vector<NamedTensor> pair;
  pair.push_back( NamedTensor{ "before", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, data.data(), 1 ) } );
  pair.push_back( NamedTensor{ "after", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, data.data(), 1 ) } );
  auto pairOut = runtime.inferNamed( pair, {} );
  REQUIRE( pairOut.size() == 2 );
  CHECK( runtime.lastMultiNames == std::vector<std::string>( { "before", "after" } ) );

  // Rank-3 refusal is typed (the bridge declares rank-4 capability only).
  std::vector<float> seq( 12, 1.f );
  std::vector<NamedTensor> ranked;
  ranked.push_back( NamedTensor{ "seq", TensorBlob::fromFloat32( { 3, 2, 2 }, seq.data(), seq.size() ) } );
  REQUIRE_THROWS_AS( runtime.inferNamed( ranked, {} ), std::runtime_error );
  try
  {
    runtime.inferNamed( ranked, {} );
  }
  catch ( const std::exception &e )
  {
    CHECK( std::string( e.what() ).find( "rank-4" ) != std::string::npos );
  }
}

TEST_CASE( "provider capabilities default to the historical contract", "[models][tensor]" )
{
  // The bridge fake does not override capabilities(): the historical default
  // is single-input, rank-4, positional, coarse cancellation.
  BridgeFakeRuntime runtime;
  const auto caps = runtime.capabilities();
  CHECK( caps.maxRank == 4 );
  CHECK( caps.cancelInForward == false );
  CHECK( caps.inputDtypes.empty() ); // empty = float32-only (documented default)
  // The OpenCV DNN provider declares named bind; fake keeps defaults.
  CHECK( runtime.framework() == "fake" );
}
