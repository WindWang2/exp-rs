// tests/test_r33_t02_gpu_pool_budget.cpp — R33-T02 WP01: the session pool's
// VRAM admission arithmetic must be checked, never wrapped. A request that
// cannot be represented inside the per-device budget (SIZE_MAX footprints,
// budget±1 boundaries, ticket sums that overflow size_t) is refused BEFORE
// the backend seam is called; reported accounting never wraps; the documented
// zero-footprint bypass and held-warm-session eviction rules are unchanged.
//
// Which cases discriminate the fix from the pre-change code: the SIZE_MAX
// probe and the saturating-sum case below fail against base (backend
// allocate calls become 2 instead of 1). The remaining cases are regression
// guards on semantics the fix must NOT change — they pass on base by design.
#include <catch2/catch_test_macros.hpp>

#include "runtime/gpu/gpu_plane.h"

#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace sicnu::runtime::gpu;

namespace
{

/// Fake backend that RECORDS every allocateVram/freeVram call (counts + sizes)
/// so the tests can prove a budget-refused request never reaches the device
/// seam and that a recycled session's VRAM is returned exactly once.
class RecordingBackend final : public GpuBackend
{
  public:
    explicit RecordingBackend( size_t totalVramMb )
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
        ++m_allocateCalls;
        m_allocateSizes.push_back( mb );
        // Single-device fake: device 0 is the only entry (the pool's device
        // selection pins 0 in every case below).
        const size_t used = m_used[deviceId];
        const size_t total = m_devices[0].totalVramMb;
        // Checked like the pool: a footprint that cannot be represented in
        // the device is refused here too (the seam's own contract).
        if ( mb > total || used > total - mb )
            return false;
        m_used[deviceId] = used + mb;
        return true;
    }
    void freeVram( int deviceId, size_t mb ) override
    {
        ++m_freeCalls;
        m_freedSizes.push_back( mb );
        m_used[deviceId] = m_used[deviceId] >= mb ? m_used[deviceId] - mb : 0;
    }
    size_t used() const
    {
        auto it = m_used.find( 0 );
        return it == m_used.end() ? 0 : it->second;
    }
    int allocateCalls() const { return m_allocateCalls; }
    int freeCalls() const { return m_freeCalls; }
    const std::vector<size_t> &allocateSizes() const { return m_allocateSizes; }
    const std::vector<size_t> &freedSizes() const { return m_freedSizes; }

  private:
    std::vector<DeviceInfo> m_devices;
    std::map<int, size_t> m_used;
    int m_allocateCalls = 0;
    int m_freeCalls = 0;
    std::vector<size_t> m_allocateSizes;
    std::vector<size_t> m_freedSizes;
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

constexpr size_t kMaxSize = std::numeric_limits<size_t>::max();

} // namespace

TEST_CASE( "GPU pool refuses an unrepresentable footprint before the backend (WP01 probe)",
           "[gpu_plane][r33t02]" )
{
    // The reproduced probe: budget 100 MiB, one live 10 MiB session, then a
    // SIZE_MAX request. The unchecked `used + mb` used to wrap to 9, pass the
    // budget test, and call the backend with SIZE_MAX.
    auto backend = std::make_shared<RecordingBackend>( 8192 );
    ModelSessionPool pool( backend, 100 );

    auto held = pool.acquireSession( makeRequest( "held", 10 ) );
    REQUIRE( held.outcome == AcquireOutcome::Acquired );
    REQUIRE( backend->allocateCalls() == 1 );

    auto hostile = pool.acquireSession( makeRequest( "hostile", kMaxSize, { kMaxSize } ) );
    REQUIRE( hostile.outcome == AcquireOutcome::CpuFallback );
    REQUIRE_FALSE( hostile.session );
    // No backend allocate for the refused ladder steps — the pool never
    // delegates its own arithmetic failure to the device seam.
    REQUIRE( backend->allocateCalls() == 1 );
    REQUIRE_FALSE( backend->allocateSizes().empty() );
    REQUIRE( backend->allocateSizes().back() == 10 );
    // The original session and its accounting are intact (used to report 9).
    REQUIRE( pool.liveSessionCount() == 1 );
    REQUIRE( pool.usedVramMb( 0 ) == 10 );
    REQUIRE( backend->used() == 10 );
}

TEST_CASE( "GPU pool delegates to the backend when the budget is disabled (budget == 0)",
           "[gpu_plane][r33t02]" )
{
    // budget == 0 means "pool accounting off": the backend seam owns refusal,
    // exactly as before this change. The device (4096) is smaller than the
    // preferred footprint, so the ladder degrades via the seam's OOM, and the
    // zero-footprint bypass still falls to CPU.
    auto backend = std::make_shared<RecordingBackend>( 4096 );
    ModelSessionPool pool( backend, 0 );

    auto reduced = pool.acquireSession( makeRequest( "segmenter", 8192, { 2048, 512 } ) );
    REQUIRE( reduced.outcome == AcquireOutcome::AcquiredReduced );
    REQUIRE( reduced.session->grantedVramMb == 2048 ); // seam's first fit

    // Even the ladder cannot fit: CPU fallback, and the pool never wraps a
    // zero budget into an admission of its own.
    auto fallback = pool.acquireSession( makeRequest( "huge", 4096, { 4096 } ) );
    REQUIRE( fallback.outcome == AcquireOutcome::CpuFallback );
    REQUIRE( pool.usedVramMb( 0 ) == 2048 );
}

TEST_CASE( "GPU pool admits exactly at the budget and refuses one above (budget±1)",
           "[gpu_plane][r33t02]" )
{
    auto backend = std::make_shared<RecordingBackend>( 8192 );
    ModelSessionPool pool( backend, 100 );

    auto exact = pool.acquireSession( makeRequest( "exact", 100 ) );
    REQUIRE( exact.outcome == AcquireOutcome::Acquired );
    REQUIRE( exact.session->grantedVramMb == 100 );

    auto over = pool.acquireSession( makeRequest( "over", 101, { 101 } ) );
    REQUIRE( over.outcome == AcquireOutcome::CpuFallback );
    REQUIRE( backend->allocateCalls() == 1 ); // only the admitted request
    REQUIRE( pool.usedVramMb( 0 ) == 100 );
}

TEST_CASE( "GPU pool saturates the live-ticket sum instead of wrapping it",
           "[gpu_plane][r33t02]" )
{
    // Two half-budget-plus footprints: the second admission must fail on the
    // no-overflow form (`used > budget - mb`) without ever computing
    // `used + mb`, and the reported ledger keeps the first footprint.
    const size_t huge = kMaxSize / 2 + 64;
    auto backend = std::make_shared<RecordingBackend>( kMaxSize );
    ModelSessionPool pool( backend, kMaxSize );

    auto first = pool.acquireSession( makeRequest( "first", huge ) );
    REQUIRE( first.outcome == AcquireOutcome::Acquired );
    auto second = pool.acquireSession( makeRequest( "second", huge, { huge } ) );
    REQUIRE( second.outcome == AcquireOutcome::CpuFallback );
    REQUIRE( backend->allocateCalls() == 1 );
    REQUIRE( pool.usedVramMb( 0 ) == huge );
}

TEST_CASE( "GPU pool keeps the documented zero-footprint CPU bypass",
           "[gpu_plane][r33t02]" )
{
    auto backend = std::make_shared<RecordingBackend>( 8192 );
    ModelSessionPool pool( backend, 100 );

    auto bypass = pool.acquireSession( makeRequest( "weightless", 0 ) );
    REQUIRE( bypass.outcome == AcquireOutcome::CpuFallback );
    REQUIRE_FALSE( bypass.session );
    REQUIRE( backend->allocateCalls() == 0 );
    REQUIRE( pool.liveSessionCount() == 0 );
}

TEST_CASE( "GPU pool never evicts a held warm session and frees exactly once",
           "[gpu_plane][r33t02]" )
{
    auto backend = std::make_shared<RecordingBackend>( 8192 );
    ModelSessionPool pool( backend, 100 );

    auto held = pool.acquireSession( makeRequest( "held", 60 ) );
    REQUIRE( held.outcome == AcquireOutcome::Acquired );
    auto other = pool.acquireSession( makeRequest( "other", 40 ) );
    REQUIRE( other.outcome == AcquireOutcome::Acquired );
    REQUIRE( pool.usedVramMb( 0 ) == 100 );

    // `held` is released (warm) but the caller still holds the shared_ptr:
    // budget pressure must NOT recycle it, and the request falls to CPU.
    pool.releaseSession( held.session->sessionId );
    auto pressured = pool.acquireSession( makeRequest( "pressured", 50, { 50 } ) );
    REQUIRE( pressured.outcome == AcquireOutcome::CpuFallback );
    REQUIRE( pool.liveSessionCount() == 2 );
    REQUIRE( pool.usedVramMb( 0 ) == 100 );
    REQUIRE( backend->used() == 100 );

    // Once the external holder drops it, the same pressure evicts the warm
    // session and admits the reduced ladder step.
    REQUIRE( backend->freeCalls() == 0 ); // nothing freed while it was held
    held.session.reset();
    auto afterEvict = pool.acquireSession( makeRequest( "pressured", 50, { 50 } ) );
    REQUIRE( afterEvict.outcome == AcquireOutcome::Acquired );
    REQUIRE( afterEvict.session->grantedVramMb == 50 );
    REQUIRE( pool.usedVramMb( 0 ) == 90 ); // 40 (other) + 50 (reduced)
    REQUIRE( backend->used() == 90 );
    REQUIRE( pool.liveSessionCount() == 2 );
    // The recycled session's VRAM was returned to the seam exactly once, for
    // exactly its footprint — no double free, no phantom release.
    REQUIRE( backend->freeCalls() == 1 );
    REQUIRE( backend->freedSizes().size() == 1 );
    REQUIRE( backend->freedSizes().back() == 60 );
}
