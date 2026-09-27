// test_runtime_gpu_boundaries_r4.cpp — Track 15 WP-B, gpu subdomain.
// Boundary oracles for ModelSessionPool (src/runtime/gpu/gpu_plane.h): device
// pinning (#1094), the per-device warm-session fairness bound, typed honesty
// of the reuse path, stale-identity recycling safety, and teardown accounting.
// The fake backend is the same seam the production pool binds at runtime; a
// multi-device variant exercises pin semantics.
#include <catch2/catch_test_macros.hpp>

#include "runtime/gpu/gpu_plane.h"

#include <map>
#include <string>
#include <vector>

using namespace sicnu::runtime::gpu;

namespace
{
class MultiDeviceBackend final : public GpuBackend
{
  public:
    explicit MultiDeviceBackend( std::vector<size_t> totalsMb )
    {
        for ( size_t i = 0; i < totalsMb.size(); ++i )
        {
            DeviceInfo device;
            device.deviceId = static_cast<int>( i );
            device.name = "fake-gpu-" + std::to_string( i );
            device.totalVramMb = totalsMb[i];
            device.available = true;
            m_devices.push_back( device );
        }
    }
    std::vector<DeviceInfo> enumerate() override { return m_devices; }
    bool allocateVram( int deviceId, size_t mb ) override
    {
        const size_t used = m_used[deviceId];
        if ( used + mb > m_devices[static_cast<size_t>( deviceId )].totalVramMb )
            return false;
        m_used[deviceId] = used + mb;
        return true;
    }
    void freeVram( int deviceId, size_t mb ) override
    {
        size_t &used = m_used[deviceId];
        used = used >= mb ? used - mb : 0;
    }
    size_t usedOn( int deviceId ) const
    {
        auto it = m_used.find( deviceId );
        return it == m_used.end() ? 0 : it->second;
    }

  private:
    std::vector<DeviceInfo> m_devices;
    std::map<int, size_t> m_used;
};

SessionRequest makeRequest( const std::string &id, size_t vramMb, int deviceId = -1,
                            std::vector<size_t> ladder = {} )
{
    SessionRequest r;
    r.model.modelId = id;
    r.model.modelPath = "/models/" + id + ".onnx";
    r.model.signature = "sig-1";
    r.vramMb = vramMb;
    r.deviceId = deviceId;
    r.reducedVramMb = std::move( ladder );
    return r;
}
} // namespace

TEST_CASE( "A device pin never reuses a warm session living on another device",
           "[runtime][gpu][r4]" )
{
    auto backend = std::make_shared<MultiDeviceBackend>( std::vector<size_t>{ 8192, 8192 } );
    ModelSessionPool pool( backend, 8192 );

    auto warm = pool.acquireSession( makeRequest( "detector", 2048 ) ); // auto → device 0
    REQUIRE( warm.outcome == AcquireOutcome::Acquired );
    REQUIRE( warm.session->deviceId == 0 );
    pool.releaseSession( warm.session->sessionId );

    // The same identity pinned to device 1 must load there — silently running
    // on the warm device-0 session would violate the pin (#1094).
    auto pinned = pool.acquireSession( makeRequest( "detector", 2048, 1 ) );
    REQUIRE( pinned.outcome == AcquireOutcome::Acquired );
    REQUIRE( pinned.session->deviceId == 1 );
    REQUIRE( pinned.session->sessionId != warm.session->sessionId );
    REQUIRE( pool.liveSessionCount() == 2 );
    REQUIRE( backend->usedOn( 0 ) == 2048 );
    REQUIRE( backend->usedOn( 1 ) == 2048 );
}

TEST_CASE( "A pin to an unknown or unavailable device is NoDevice without side effects",
           "[runtime][gpu][r4]" )
{
    auto backend = std::make_shared<MultiDeviceBackend>( std::vector<size_t>{ 8192 } );
    ModelSessionPool pool( backend, 8192 );

    auto ghost = pool.acquireSession( makeRequest( "detector", 2048, 7 ) );
    REQUIRE( ghost.outcome == AcquireOutcome::NoDevice );
    REQUIRE_FALSE( ghost.session );
    REQUIRE( pool.liveSessionCount() == 0 );
    REQUIRE( backend->usedOn( 0 ) == 0 );
}

TEST_CASE( "The per-device warm-session bound reports Busy, never over-commits, and recovers after recycling",
           "[runtime][gpu][r4]" )
{
    auto backend = std::make_shared<MultiDeviceBackend>( std::vector<size_t>{ 65536 } );
    ModelSessionPool pool( backend, 65536 ); // VRAM budget not the limiter here

    std::vector<std::string> ids;
    for ( int i = 0; i < 4; ++i )
    {
        auto session = pool.acquireSession( makeRequest( "m" + std::to_string( i ), 1024 ) );
        REQUIRE( session.outcome == AcquireOutcome::Acquired );
        ids.push_back( session.session->sessionId );
    }
    // Fifth concurrent load on the same device: fairness bound, typed Busy.
    auto fifth = pool.acquireSession( makeRequest( "m5", 1024 ) );
    REQUIRE( fifth.outcome == AcquireOutcome::Busy );
    REQUIRE_FALSE( fifth.session );
    REQUIRE( pool.liveSessionCount() == 4 );

    // A released (warm) session keeps its slot: the bound counts every
    // VRAM-resident session on the device, warm or held.
    pool.releaseSession( ids[0] );
    auto stillBusy = pool.acquireSession( makeRequest( "m5", 1024 ) );
    REQUIRE( stillBusy.outcome == AcquireOutcome::Busy );

    // Recycling the stale warm copy reopens the slot for a real load.
    pool.evictStale( "m0", "sig-changed" );
    REQUIRE( pool.liveSessionCount() == 3 );
    auto afterRecycle = pool.acquireSession( makeRequest( "m5", 1024 ) );
    REQUIRE( afterRecycle.outcome == AcquireOutcome::Acquired );
    REQUIRE( pool.liveSessionCount() == 4 );
}

TEST_CASE( "Reuse with a smaller granted footprint than requested reports AcquiredReduced",
           "[runtime][gpu][r4]" )
{
    auto backend = std::make_shared<MultiDeviceBackend>( std::vector<size_t>{ 8192 } );
    ModelSessionPool pool( backend, 8192 );

    auto small = pool.acquireSession( makeRequest( "detector", 1024 ) );
    REQUIRE( small.outcome == AcquireOutcome::Acquired );
    REQUIRE( small.session->grantedVramMb == 1024 );
    pool.releaseSession( small.session->sessionId );

    // Same identity, bigger preferred footprint: the warm 1024 MB session is
    // still the right reuse (identity-keyed), but the outcome must not claim
    // the preferred footprint was granted.
    auto bigger = pool.acquireSession( makeRequest( "detector", 4096 ) );
    REQUIRE( bigger.outcome == AcquireOutcome::AcquiredReduced );
    REQUIRE( bigger.session );
    REQUIRE( bigger.session->sessionId == small.session->sessionId );
    REQUIRE( bigger.session->grantedVramMb == 1024 );

    // An equal-size re-request keeps the plain Acquired verdict.
    pool.releaseSession( bigger.session->sessionId );
    auto equal = pool.acquireSession( makeRequest( "detector", 1024 ) );
    REQUIRE( equal.outcome == AcquireOutcome::Acquired );
}

TEST_CASE( "A stale identity is not reused; the old warm session waits for evictStale",
           "[runtime][gpu][r4]" )
{
    auto backend = std::make_shared<MultiDeviceBackend>( std::vector<size_t>{ 8192 } );
    ModelSessionPool pool( backend, 8192 );

    auto old = pool.acquireSession( makeRequest( "detector", 1024 ) );
    REQUIRE( old.outcome == AcquireOutcome::Acquired );
    pool.releaseSession( old.session->sessionId );

    SessionRequest changed = makeRequest( "detector", 1024 );
    changed.model.signature = "sig-2";
    auto fresh = pool.acquireSession( changed );
    REQUIRE( fresh.outcome == AcquireOutcome::Acquired );
    REQUIRE( fresh.session->sessionId != old.session->sessionId );
    // Both copies are VRAM-resident until the stale one is recycled.
    REQUIRE( pool.liveSessionCount() == 2 );
    REQUIRE( backend->usedOn( 0 ) == 2048 );

    // Release protocol: after releaseSession the operator drops its handle —
    // only then is the stale session recyclable (the #1094 use_count gate
    // must never free VRAM under a held session).
    old.session.reset();
    pool.evictStale( "detector", "sig-2" );
    REQUIRE( pool.liveSessionCount() == 1 );
    REQUIRE( backend->usedOn( 0 ) == 1024 );
    REQUIRE( fresh.session->useCount == 1 );
}

TEST_CASE( "evictStale never recycles a session another operator still holds",
           "[runtime][gpu][r4]" )
{
    auto backend = std::make_shared<MultiDeviceBackend>( std::vector<size_t>{ 8192 } );
    ModelSessionPool pool( backend, 8192 );

    auto held = pool.acquireSession( makeRequest( "detector", 1024 ) );
    REQUIRE( held.outcome == AcquireOutcome::Acquired );
    // A second operator shares the session, then the first returns it.
    auto sharedCopy = held.session;
    pool.releaseSession( held.session->sessionId );

    // The model file changed: eviction must skip the held session — freeing
    // its VRAM would pull device memory out from under a live inference.
    pool.evictStale( "detector", "sig-2" );
    REQUIRE( pool.liveSessionCount() == 1 );
    REQUIRE( backend->usedOn( 0 ) == 1024 );

    // The last external reference drops; now the warm session is recyclable.
    sharedCopy.reset();
    held.session.reset();
    pool.evictStale( "detector", "sig-2" );
    REQUIRE( pool.liveSessionCount() == 0 );
    REQUIRE( backend->usedOn( 0 ) == 0 );
}

TEST_CASE( "Pool teardown returns every VRAM reservation to the backend",
           "[runtime][gpu][r4]" )
{
    auto backend = std::make_shared<MultiDeviceBackend>( std::vector<size_t>{ 8192 } );
    {
        ModelSessionPool pool( backend, 8192 );
        auto a = pool.acquireSession( makeRequest( "a", 1024 ) );
        auto b = pool.acquireSession( makeRequest( "b", 512 ) );
        REQUIRE( a.outcome == AcquireOutcome::Acquired );
        REQUIRE( b.outcome == AcquireOutcome::Acquired );
        REQUIRE( backend->usedOn( 0 ) == 1536 );
    }
    REQUIRE( backend->usedOn( 0 ) == 0 );
}

TEST_CASE( "A request with no footprint and no ladder degrades to CPU, and an empty model id is refused",
           "[runtime][gpu][r4]" )
{
    auto backend = std::make_shared<MultiDeviceBackend>( std::vector<size_t>{ 8192 } );
    ModelSessionPool pool( backend, 8192 );

    // Zero preferred VRAM with an empty ladder is a CPU-shaped request: no
    // admission attempt, typed CpuFallback (never Busy, never a session).
    auto cpuShaped = pool.acquireSession( makeRequest( "zero", 0 ) );
    REQUIRE( cpuShaped.outcome == AcquireOutcome::CpuFallback );
    REQUIRE_FALSE( cpuShaped.session );
    REQUIRE( pool.liveSessionCount() == 0 );

    // An empty model id has no identity to pool by: NoDevice even though a
    // device exists.
    SessionRequest anonymous = makeRequest( "", 1024 );
    REQUIRE( pool.acquireSession( anonymous ).outcome == AcquireOutcome::NoDevice );
}
