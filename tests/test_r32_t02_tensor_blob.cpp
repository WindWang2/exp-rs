// tests/test_r32_t02_tensor_blob.cpp — R32-T02 WP13 (C01).
//
// TensorBlob::isValid() checked only the int64 ELEMENT product
// (countOrZero), while expectedByteCount() multiplied the dtype size in
// UNCHECKED size_t — on a 64-bit host a wire shape like {2^62} float32
// wraps the byte product to 2^64 ≡ 0, so an EMPTY payload validated as a
// structurally sound 2^62-element tensor (exactly the wrap countOrZero
// cannot see: 2^62 fits int64). decodeWireTensor — the HTTP/Python wire
// gate — trusted that verdict. The element→byte capacity is now checked in
// the same 64-bit domain, and the factories (zeros/fromFloat32) share the
// checked capacity instead of wrapping their own allocations. Transport
// precision (float64 exact, int64 typed refusal — never bit-cast) is
// pinned alongside.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/runtime/tensor_blob.h"

#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using sicnu::operators::runtime::TensorBlob;
using sicnu::operators::runtime::TensorDType;

constexpr std::int64_t kTwoPow62 = std::int64_t{ 1 } << 62;

} // namespace

TEST_CASE( "R32-T02 WP13: byte-product wrap cannot validate an empty payload (C01)",
           "[r32t02][wp13][tensor]" )
{
  // {2^62} float32: element count FITS int64 (countOrZero passes), byte
  // product 2^62·4 wraps size_t to 0 — the pre-fix isValid() returned TRUE
  // for zero bytes of a declared 4·2^62-byte tensor.
  TensorBlob wrap;
  wrap.shape = { kTwoPow62 };
  wrap.dtype = TensorDType::Float32; // bytes stays empty
  CHECK_FALSE( wrap.isValid() );

  // Multi-dim wrap: {2^61, 2} float32 → same 2^64 byte product.
  TensorBlob wrapMulti;
  wrapMulti.shape = { kTwoPow62 / 2, 2 };
  wrapMulti.dtype = TensorDType::Float32;
  CHECK_FALSE( wrapMulti.isValid() );

  // float64 wraps one power earlier: {2^61}·8 = 2^64.
  TensorBlob wrap64;
  wrap64.shape = { kTwoPow62 / 2 };
  wrap64.dtype = TensorDType::Float64;
  CHECK_FALSE( wrap64.isValid() );

  // Precision control — not every huge shape is refused, only ones whose
  // byte product is unrepresentable: {2^62} UINT8 has a representable
  // (2^62-byte) expectation that a small payload simply fails to match.
  TensorBlob hugeExact;
  hugeExact.shape = { kTwoPow62 };
  hugeExact.dtype = TensorDType::UInt8;
  CHECK_FALSE( hugeExact.isValid() );

  // Valid controls: byte count EXACTLY matching keeps validating.
  TensorBlob ok;
  ok.shape = { 2, 4 };
  ok.dtype = TensorDType::Float32;
  ok.bytes.assign( 2 * 4 * 4, 0x3f );
  CHECK( ok.isValid() );

  // The invalid blobs refuse their views with typed errors (consumers
  // validate before reading — dataFloat32/toMat must not hand out bytes).
  REQUIRE_THROWS_AS( wrap.dataFloat32(), std::runtime_error );
  REQUIRE_THROWS_AS( wrap.toMat(), std::runtime_error );
}

TEST_CASE( "R32-T02 WP13: factories share the checked element→byte capacity",
           "[r32t02][wp13][tensor]" )
{
  // zeros(): an absurd declared shape stays a DECLARED but structurally
  // invalid blob (documented contract) — never a wrapped allocation that
  // used to silently assign 0 bytes for {2^62} float32.
  const TensorBlob absurd = TensorBlob::zeros( { kTwoPow62 }, TensorDType::Float32 );
  CHECK_FALSE( absurd.isValid() );
  CHECK( absurd.bytes.empty() );

  // zeros() control: a real shape allocates exactly.
  const TensorBlob z = TensorBlob::zeros( { 3, 5 }, TensorDType::Float32 );
  CHECK( z.isValid() );
  CHECK( z.bytes.size() == 3 * 5 * 4 );

  // fromFloat32(): a count whose float32 payload wraps size_t is a typed
  // refusal, never a truncated buffer (nullptr data is fine — the guard
  // fires before any copy).
  REQUIRE_THROWS_AS(
    TensorBlob::fromFloat32( { 4 }, nullptr, std::numeric_limits<std::size_t>::max() / 2 ),
    std::runtime_error );

  // fromFloat32() control: exact copy of the payload.
  const float src[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
  const TensorBlob f = TensorBlob::fromFloat32( { 2, 2 }, src, 4 );
  CHECK( f.isValid() );
  const float *view = f.dataFloat32();
  CHECK( view[3] == 4.0f );
}

TEST_CASE( "R32-T02 WP13: transport precision — float64 exact, int64 refused (no bit-cast)",
           "[r32t02][wp13][tensor]" )
{
  // A float64 tensor transports REAL doubles: byte-exact round trip through
  // fromMat/toMat (CV_64F), never a float32 reinterpretation.
  const double src[3] = { 0.1, 1.0 / 3.0, 1e18 };
  cv::Mat mat( 1, 3, CV_64F, const_cast<double *>( src ) );
  const TensorBlob blob = TensorBlob::fromMat( mat );
  REQUIRE( blob.isValid() );
  REQUIRE( blob.dtype == TensorDType::Float64 );
  CHECK( std::memcmp( blob.bytes.data(), src, sizeof( src ) ) == 0 );
  const cv::Mat back = blob.toMat();
  REQUIRE( back.depth() == CV_64F );
  const double *vals = back.ptr<double>( 0 );
  CHECK( vals[0] == Catch::Approx( 0.1 ).margin( 0.0 ) );
  CHECK( vals[1] == Catch::Approx( 1.0 / 3.0 ).margin( 0.0 ) );
  CHECK( vals[2] == Catch::Approx( 1e18 ).margin( 0.0 ) );

  // int64 has no exact cv depth: toMat refuses (typed), never bit-casts.
  TensorBlob i64;
  i64.shape = { 2 };
  i64.dtype = TensorDType::Int64;
  i64.bytes.resize( 2 * sizeof( std::int64_t ) );
  CHECK( i64.isValid() );
  REQUIRE_THROWS_AS( i64.toMat(), std::runtime_error );
}
