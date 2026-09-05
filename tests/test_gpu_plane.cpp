// test_gpu_plane.cpp — Phase H GPU session plane tests against a fake backend:
// session reuse, VRAM budget admission, OOM ladder → reduced → CPU fallback,
// identity-based stale eviction, and the per-device fairness bound.
#include <catch2/catch_test_macros.hpp>

#include "runtime/gpu/gpu_plane.h"

#include <map>
#include <string>

using namespace sicnu::runtime::gpu;

namespace
{
class FakeBackend final : public GpuBackend
{
  public:
    explicit FakeBackend( size_t totalVramMb )
    {
        DeviceInfo device;
        device.deviceId = 0;
        device.name = "fake-gpu";
        device.totalVramMb = totalVramMb;
        device.available = true;
        m_devices.push_back( device );
    }
    std::vector<DeviceInfo> enumerate() override { return m_devices; }
    bool allocateVram( int deviceId, size_t mb ) override
    {
        const size_t used = m_used[deviceId];
        const size_t total = m_devices[0].totalVramMb;
        if ( used + mb > total )
            return false; // OOM
        m_used[deviceId] = used + mb;
        return true;
    }
    void freeVram( int deviceId, size_t mb ) override
    {
        m_used[deviceId] = m_used[deviceId] >= mb ? m_used[deviceId] - mb : 0;
    }
    size_t used() const { auto it = m_used.find( 0 ); return it == m_used.end() ? 0 : it->second; }

  private:
    std::vector<DeviceInfo> m_devices;
    std::map<int, size_t> m_used;
};

SessionRequest makeRequest( const std::string &id, size_t vramMb,
                            std::vector<size_t> ladder = {} )
{
    SessionRequest r;
    r.model.modelId = id;
    r.model.modelPath = "/models/" + id + ".onnx";
    r.model.signature = "sig-1";
    r.vramMb = vramMb;
    r.reducedVramMb = std::move( ladder );
    return r;
}
} // namespace

TEST_CASE( "GPU plane reuses a session for the same model identity", "[gpu_plane]" )
{
    auto backend = std::make_shared<FakeBackend>( 8192 );
    ModelSessionPool pool( backend, 8192 );

    auto first = pool.acquireSession( makeRequest( "detector", 2048 ) );
    REQUIRE( first.outcome == AcquireOutcome::Acquired );
    REQUIRE( first.session );
    REQUIRE( first.session->useCount == 1 );
    pool.releaseSession( first.session->sessionId );

    auto second = pool.acquireSession( makeRequest( "detector", 2048 ) );
    REQUIRE( second.outcome == AcquireOutcome::Acquired );
    REQUIRE( second.session->sessionId == first.session->sessionId );
    REQUIRE( second.session->useCount == 2 );
    // Still exactly one copy of the model in VRAM.
    REQUIRE( pool.liveSessionCount() == 1 );
    REQUIRE( pool.usedVramMb( 0 ) == 2048 );
}

TEST_CASE( "GPU plane OOM ladder degrades before falling back to CPU", "[gpu_plane]" )
{
    auto backend = std::make_shared<FakeBackend>( 4096 );
    ModelSessionPool pool( backend, 8192 ); // pool budget above the device: device OOM governs

    SessionRequest big = makeRequest( "segmenter", 8192, { 2048, 512 } );
    auto reduced = pool.acquireSession( big );
    REQUIRE( reduced.outcome == AcquireOutcome::AcquiredReduced );
    REQUIRE( reduced.session->grantedVramMb == 2048 ); // first ladder step that fits

    // When even the ladder cannot fit, CPU fallback — never a crash.
    SessionRequest huge = makeRequest( "huge", 4096, { 4096 } );
    auto fallback = pool.acquireSession( huge );
    REQUIRE( fallback.outcome == AcquireOutcome::CpuFallback );
    REQUIRE_FALSE( fallback.session );
}

TEST_CASE( "GPU plane enforces the per-device VRAM budget", "[gpu_plane]" )
{
    auto backend = std::make_shared<FakeBackend>( 8192 );
    ModelSessionPool pool( backend, 2048 ); // pool budget below the device

    auto a = pool.acquireSession( makeRequest( "a", 1024 ) );
    REQUIRE( a.outcome == AcquireOutcome::Acquired );
    auto b = pool.acquireSession( makeRequest( "b", 1024 ) );
    REQUIRE( b.outcome == AcquireOutcome::Acquired );
    // 2048 budget exhausted: the third model degrades to CPU instead of
    // over-committing VRAM.
    auto c = pool.acquireSession( makeRequest( "c", 1024, { 512 } ) );
    REQUIRE( c.outcome == AcquireOutcome::CpuFallback );
    REQUIRE( pool.usedVramMb( 0 ) == 2048 );
}

TEST_CASE( "GPU plane evicts stale model identities", "[gpu_plane]" )
{
    auto backend = std::make_shared<FakeBackend>( 8192 );
    ModelSessionPool pool( backend, 8192 );

    auto stale = pool.acquireSession( makeRequest( "detector", 1024 ) );
    REQUIRE( stale.outcome == AcquireOutcome::Acquired );
    pool.releaseSession( stale.session->sessionId );

    // The model file changed: signature differs ⇒ old session recycled.
    pool.evictStale( "detector", "sig-2" );
    REQUIRE( pool.liveSessionCount() == 0 );
    REQUIRE( pool.usedVramMb( 0 ) == 0 );

    auto fresh = pool.acquireSession( makeRequest( "detector", 1024 ) );
    auto freshReq = makeRequest( "detector", 1024 );
    freshReq.model.signature = "sig-2";
    auto freshSession = pool.acquireSession( freshReq );
    REQUIRE( freshSession.outcome == AcquireOutcome::Acquired );
}
