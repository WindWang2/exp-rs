// tests/test_r35_t05_agent_runtime_bounds.cpp
//
// R35-T05 WP14 (C++ side): SharedMemorySegment::create() must refuse a
// dimension product that wraps size_t. The multiply chain
// width*height*bands*elemSize used to wrap silently — e.g.
// 2^30 * 2^30 * 4 * 4 bytes == 2^64 wraps to 0 — and a wrapped payload of 0
// passed the total-size sanity check as a 32-byte segment that CLAIMED
// absurd dimensions (the mirror of the attach()-side untrusted-header gates
// covered by test_r28_t03_shm_manifest_gates.cpp).
#include <catch2/catch_test_macros.hpp>

#include "python/isolated/shared_memory_segment.h"

#include <cstring>

using sicnu::python::isolated::SharedMemorySegment;

namespace
{

SharedMemorySegment::Header readHeader( SharedMemorySegment &segment )
{
    SharedMemorySegment::Header hdr{};
    void *payload = segment.payload();
    REQUIRE( payload != nullptr );
    std::memcpy( &hdr, static_cast< char * >( payload ) - sizeof( hdr ), sizeof( hdr ) );
    return hdr;
}

} // namespace

TEST_CASE( "create refuses a dimension product that wraps size_t",
           "[agent][python][shm][r35_t05]" )
{
    SharedMemorySegment segment;
    // 2^30 * 2^30 * 4 elements * 4 bytes/element == 2^64 -> wraps to 0.
    // Pre-fix this returned true with a 32-byte mapping, payloadSize()==0
    // and a header claiming 2^30 x 2^30 x 4 float32.
    CHECK_FALSE( segment.create( 1 << 30, 1 << 30, 4,
                                 SharedMemorySegment::DType::Float32 ) );
    CHECK_FALSE( segment.isAttached() );
    CHECK( segment.payloadSize() == 0 );
}

TEST_CASE( "create still allocates legal dimensions and stamps the header",
           "[agent][python][shm][r35_t05]" )
{
    SharedMemorySegment segment;
    REQUIRE( segment.create( 3, 2, 1, SharedMemorySegment::DType::Float32 ) );
    CHECK( segment.isAttached() );
    CHECK( segment.payloadSize() == 3 * 2 * 1 * 4 );

    const SharedMemorySegment::Header hdr = readHeader( segment );
    CHECK( hdr.width == 3 );
    CHECK( hdr.height == 2 );
    CHECK( hdr.bands == 1 );
    CHECK( hdr.dtype == static_cast<int32_t>( SharedMemorySegment::DType::Float32 ) );

    // A second instance attaches through the validated path: the create-side
    // guard changed nothing about the wire format.
    SharedMemorySegment reader;
    REQUIRE( reader.attach( segment.key() ) );
    CHECK( reader.payloadSize() == 3 * 2 * 1 * 4 );
    reader.detach();
    segment.detach();
}
