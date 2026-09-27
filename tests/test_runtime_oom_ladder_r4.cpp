// test_runtime_oom_ladder_r4.cpp — Track 15 WP-C: the OOM/degradation ladder
// at the REAL runtime seams. "Real" means the constraint that forces the
// downgrade is the production accounting itself — the planner's byte budget
// and the pool backend's VRAM ledger — never a mock failure injection:
//
//   planner rungs (chunk::planTileMemory via ExecutionGovernor):
//     Admit → ReduceConcurrency → Spill → Refuse (typed exception)
//   pool rungs (ModelSessionPool against a real ledger backend):
//     Acquired → AcquiredReduced → CpuFallback
//
// Every rung must be reachable by shrinking ONE real budget, and every
// downgrade must leave an observable trace (typed plan action / granted
// footprint / structured reason). The hardware-VRAM rung of the operators'
// tile engine (batch → tile-by-tile) is NOT duplicated here: it is covered by
// the failure-matrix suite through the provider fault seam and requires a GPU
// quota facility this host does not have — recorded as "simulated layer" in
// PROVIDER_OOM_MATRIX.md, honestly annotated, not re-faked here.
#include <catch2/catch_test_macros.hpp>

#include "runtime/exec/execution_governor.h"
#include "runtime/gpu/gpu_plane.h"

#include <map>
#include <string>
#include <vector>

using namespace sicnu::runtime::chunk;
using namespace sicnu::runtime::exec;
using namespace sicnu::runtime::gpu;

namespace
{
// Reference stream shape: 256x256 float32 tiles, linear chain, capacity 2.
constexpr std::uint64_t kPerTile = 256ull * 256ull * 4ull;
constexpr std::uint64_t kPeakCap2 = 5ull * kPerTile;
constexpr std::uint64_t kPeakCap1 = 4ull * kPerTile;

TileMemoryRequest referenceRequest()
{
    TileMemoryRequest request;
    request.tileWidth = 256;
    request.tileHeight = 256;
    request.bands = 1;
    request.bytesPerSample = 4;
    request.inputCount = 1;
    request.stageCount = 0;
    request.requestedQueueCapacity = 2;
    return request;
}

class LedgerBackend final : public GpuBackend
{
  public:
    explicit LedgerBackend( size_t totalVramMb )
    {
        DeviceInfo device;
        device.deviceId = 0;
        device.name = "ledger-gpu";
        device.totalVramMb = totalVramMb;
        device.available = true;
        m_devices.push_back( device );
    }
    std::vector<DeviceInfo> enumerate() override { return m_devices; }
    bool allocateVram( int, size_t mb ) override
    {
        if ( m_used + mb > m_devices[0].totalVramMb )
            return false; // the real device-OOM branch
        m_used += mb;
        return true;
    }
    void freeVram( int, size_t mb ) override
    {
        if ( m_used >= mb )
            m_used -= mb;
    }
    size_t used() const { return m_used; }

  private:
    std::vector<DeviceInfo> m_devices;
    size_t m_used = 0;
};

SessionRequest makeRequest( const std::string &id, size_t vramMb, std::vector<size_t> ladder )
{
    SessionRequest request;
    request.model.modelId = id;
    request.model.modelPath = "/models/" + id + ".onnx";
    request.model.signature = "sig-1";
    request.vramMb = vramMb;
    request.reducedVramMb = std::move( ladder );
    return request;
}
} // namespace

TEST_CASE( "Planner degradation ladder: every rung is reachable by shrinking one real budget",
           "[runtime][oom][r4]" )
{
    ExecutionGovernor governor( ExecutionGovernor::Config{} );

    // Rung 0 — Admit: the request fits the real budget with no changes.
    {
        TileMemoryRequest request = referenceRequest();
        request.budgetBytes = kPeakCap2;
        const TileMemoryPlan plan = governor.admitOrRefuse( request );
        REQUIRE( plan.action == TileMemoryPlan::Action::Admit );
        REQUIRE( plan.reason.empty() ); // no degradation, no story to tell
    }

    // Rung 1 — ReduceConcurrency: budget -1 byte forces the minimal queue
    // shape; the plan RECOMMENDS the degraded shape and explains why.
    {
        TileMemoryRequest request = referenceRequest();
        request.budgetBytes = kPeakCap2 - 1;
        const TileMemoryPlan plan = governor.admitOrRefuse( request );
        REQUIRE( plan.action == TileMemoryPlan::Action::ReduceConcurrency );
        REQUIRE( plan.recommendedQueueCapacity == 1 );
        REQUIRE( plan.estimatedPeakBytes == kPeakCap1 );
        // Degradation trace: names the requested shape, the budget and the
        // reduced shape.
        REQUIRE( plan.reason.find( "queueCapacity=2" ) != std::string::npos );
        REQUIRE( plan.reason.find( "queueCapacity=1" ) != std::string::npos );
        REQUIRE( plan.reason.find( std::to_string( request.budgetBytes ) ) != std::string::npos );
    }

    // Rung 2 — Spill: below the RAM-minimal shape, real scratch budget covers
    // the intermediates; the spill NEED is the trace.
    {
        TileMemoryRequest request = referenceRequest();
        request.budgetBytes = kPeakCap1 - 1;
        request.allowSpill = true;
        request.expectedTileCount = 4;
        request.scratchBudgetBytes = 4 * kPerTile;
        const TileMemoryPlan plan = governor.admitOrRefuse( request );
        REQUIRE( plan.action == TileMemoryPlan::Action::Spill );
        REQUIRE( plan.spillBytes == 4 * kPerTile );
        REQUIRE( plan.reason.find( std::to_string( plan.spillBytes ) ) != std::string::npos );
    }

    // Rung 3 — Refuse: one byte short on scratch too. The typed exception is
    // the refusal; nothing ran.
    {
        TileMemoryRequest request = referenceRequest();
        request.budgetBytes = kPeakCap1 - 1;
        request.allowSpill = true;
        request.expectedTileCount = 4;
        request.scratchBudgetBytes = 4 * kPerTile - 1;
        REQUIRE_THROWS_AS( governor.admitOrRefuse( request ), AdmissionRefused );
    }
}

TEST_CASE( "Pool degradation ladder: real VRAM ledger forces preferred → reduced → CPU",
           "[runtime][oom][r4]" )
{
    // Device with a hard 3072 MB ledger; the pool budget never binds. The
    // ladder is forced purely by real device capacity.
    auto backend = std::make_shared<LedgerBackend>( 3072 );
    ModelSessionPool pool( backend, 65536 );

    // Rung 0 — Acquired: the preferred footprint fits the ledger.
    {
        AcquireResult result = pool.acquireSession( makeRequest( "model-a", 1024, {} ) );
        REQUIRE( result.outcome == AcquireOutcome::Acquired );
        REQUIRE( result.session->grantedVramMb == 1024 );
        pool.releaseSession( result.session->sessionId );
        result.session.reset(); // release protocol: drop the handle (#1094 gate)
        pool.evictStale( "model-a", "sig-spent" );
        REQUIRE( backend->used() == 0 );
    }

    // Rung 1 — AcquiredReduced: the preferred footprint (4096) no longer fits
    // the 3072 ledger; the first ladder step that fits is granted (observable
    // via the granted footprint and the ledger balance — the downgrade is
    // real, not nominal).
    {
        SessionRequest request = makeRequest( "model-b", 4096, { 1536, 1024, 512 } );
        const AcquireResult result = pool.acquireSession( request );
        REQUIRE( result.outcome == AcquireOutcome::AcquiredReduced );
        REQUIRE( result.session->grantedVramMb == 1536 ); // first step that fits
        REQUIRE( backend->used() == 1536 );               // the ledger shows it
    }

    // Rung 2 — CpuFallback: make even the smallest step not fit by filling
    // the ledger with a held session, then try a fresh identity.
    {
        const AcquireResult filler = pool.acquireSession( makeRequest( "filler", 1536, {} ) );
        REQUIRE( filler.outcome == AcquireOutcome::Acquired );
        REQUIRE( backend->used() == 3072 ); // ledger full

        const AcquireResult fallback =
            pool.acquireSession( makeRequest( "model-c", 1024, { 512 } ) );
        REQUIRE( fallback.outcome == AcquireOutcome::CpuFallback );
        REQUIRE_FALSE( fallback.session ); // no partial grant, no leak
        REQUIRE( backend->used() == 3072 );
    }
}
