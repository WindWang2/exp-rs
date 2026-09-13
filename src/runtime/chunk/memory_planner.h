// memory_planner.h — Tile working-set planner (LSEE 10.0, ADR 0148 §3).
//
// Pure, Qt-free accounting: converts the shape of a tile stream (tile
// geometry, halo, band subset, stage/join topology, queue capacities) into a
// peak-RAM estimate and a RECOMMENDED shape that fits a byte budget. The
// decision ladder mirrors the contract: fit at the requested shape → Admit;
// else fit with minimal queues → ReduceConcurrency; else spill to scratch →
// Spill; else Refuse with a structured need/have payload — so callers never
// discover the working set via std::bad_alloc.
//
// All arithmetic is overflow-safe: an overflowing estimate saturates
// ((UINT64_MAX) rather than wrapping to a small bogus number, which always
// refuses instead of silently over-admitting. Local helpers mirror
// src/processing/framework/resource_estimation.h (that header lives in the
// processing layer; the runtime layer must not include upward).
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace sicnu::runtime::chunk
{

/// Overflow-safe product; saturates at UINT64_MAX instead of wrapping.
inline std::uint64_t saturatingMul( std::uint64_t a, std::uint64_t b )
{
    if ( a == 0 || b == 0 )
        return 0;
    if ( a > std::numeric_limits<std::uint64_t>::max() / b )
        return std::numeric_limits<std::uint64_t>::max();
    return a * b;
}

inline std::uint64_t saturatingAdd( std::uint64_t a, std::uint64_t b )
{
    const std::uint64_t sum = a + b;
    return sum < a ? std::numeric_limits<std::uint64_t>::max() : sum;
}

/// Shape of the tile stream to plan for. All counts are node topology facts
/// the caller already knows (grid spec + graph structure).
struct TileMemoryRequest
{
    std::uint32_t tileWidth = 256;        ///< nominal tile width (pixels)
    std::uint32_t tileHeight = 256;       ///< nominal tile height (pixels)
    std::uint32_t haloPixels = 0;         ///< neighborhood radius per side
    std::uint32_t bands = 1;              ///< bands carried per tile payload
    std::uint32_t bytesPerSample = 4;     ///< float32 by default
    std::uint32_t inputCount = 1;         ///< join width (1 = linear chain)
    std::uint32_t stageCount = 0;         ///< transform+join nodes between sources and sink
    std::uint32_t requestedQueueCapacity = 2; ///< per-queue bound the caller wants
    std::uint64_t globalStateBytes = 0;   ///< pass-1 global statistic (reduction state)
    std::uint64_t expectedTileCount = 0;  ///< 0 = unknown (spill need reported per-tile)
    /// RAM budget for the whole stream (bytes). 0 = advisory mode: the plan
    /// reports the requested shape's estimate and always "fits".
    std::uint64_t budgetBytes = 0;
    std::uint64_t scratchBudgetBytes = 0; ///< usable scratch disk for Spill
    bool allowSpill = false;              ///< external-memory operators only
};

struct TileMemoryPlan
{
    enum class Action
    {
        Admit,             ///< requested shape fits the budget
        ReduceConcurrency, ///< fits only with minimal queue capacity
        Spill,             ///< RAM-minimal shape still overflows; scratch covers it
        Refuse,            ///< nothing within (budget, scratch) admits this shape
        Advisory,          ///< budgetBytes == 0: estimate reported, never gates
    };

    Action action = Action::Advisory;
    /// Peak RAM at the RECOMMENDED shape (the shape the caller should run).
    std::uint64_t estimatedPeakBytes = 0;
    /// Peak RAM at the REQUESTED shape (diagnostics: what was refused/reduced).
    std::uint64_t requestedPeakBytes = 0;
    /// Bytes of scratch the Spill action needs (0 otherwise). When
    /// expectedTileCount was 0 this is the per-tile spill rate instead.
    std::uint64_t spillBytes = 0;
    std::uint32_t recommendedQueueCapacity = 2;
    /// Structured, actionable hold reason (empty when admitted). States need
    /// vs have and the concrete knobs, e.g.
    /// "tile stream needs 384 MiB, budget 128 MiB; reduce tileWidth/tileHeight
    /// (256x256 halo 8 bands 4) or raise the RAM budget".
    std::string reason;
    bool fits() const { return action == Action::Admit || action == Action::Advisory
                                || action == Action::ReduceConcurrency; }
};

/// Peak-RAM model: (stageCount+1) source-to-consumer queue stages, each
/// holding queueCapacity tiles per input, plus one tile in the consumer and
/// the pass-1 global state. Every product saturates on overflow.
///
/// HONESTY NOTE (F-A-13): this is a heuristic working-set model, not a
/// strict upper bound — it omits each stage thread's in-hand tile (a linear
/// chain's true peak is ≈ (S+1)·cap + S + 1, the model gives (S+1)·cap + 1)
/// and applies the join width to every stage. Good enough for advisory
/// preflight planning; before wiring it as a HARD admission gate, extend
/// the model with the per-stage in-hand term.
std::uint64_t tileStreamPeakBytes( const TileMemoryRequest &request, std::uint32_t queueCapacity );

/// Plans the working set per the contract ladder (see TileMemoryPlan).
TileMemoryPlan planTileMemory( const TileMemoryRequest &request );

// ── inline implementation ─────────────────────────────────────────────────

inline std::uint64_t tileStreamPeakBytes( const TileMemoryRequest &request,
                                          std::uint32_t queueCapacity )
{
    const std::uint64_t bufferWidth =
        static_cast<std::uint64_t>( request.tileWidth ) + 2ull * request.haloPixels;
    const std::uint64_t bufferHeight =
        static_cast<std::uint64_t>( request.tileHeight ) + 2ull * request.haloPixels;
    const std::uint64_t perTile = saturatingMul(
        saturatingMul( saturatingMul( bufferWidth, bufferHeight ), request.bands ),
        request.bytesPerSample );
    const std::uint64_t stages = static_cast<std::uint64_t>( request.stageCount ) + 1;
    const std::uint64_t inFlight = saturatingMul(
        saturatingMul( stages, queueCapacity ), std::max<std::uint64_t>( request.inputCount, 1 ) );
    // +1 tile resident in the consumer / writer slot.
    return saturatingAdd( saturatingAdd( saturatingMul( inFlight, perTile ), perTile ),
                          request.globalStateBytes );
}

inline TileMemoryPlan planTileMemory( const TileMemoryRequest &request )
{
    TileMemoryPlan plan;
    TileMemoryRequest shaped = request;
    const bool advisory = request.budgetBytes == 0;
    const std::uint32_t requestedCap =
        request.requestedQueueCapacity == 0 ? 2 : request.requestedQueueCapacity;

    plan.requestedPeakBytes = tileStreamPeakBytes( request, requestedCap );

    if ( advisory )
    {
        plan.action = TileMemoryPlan::Action::Advisory;
        plan.estimatedPeakBytes = plan.requestedPeakBytes;
        plan.recommendedQueueCapacity = requestedCap;
        return plan;
    }

    if ( plan.requestedPeakBytes <= request.budgetBytes )
    {
        plan.action = TileMemoryPlan::Action::Admit;
        plan.estimatedPeakBytes = plan.requestedPeakBytes;
        plan.recommendedQueueCapacity = requestedCap;
        return plan;
    }

    // Minimal shape: one tile per queue per input.
    constexpr std::uint32_t kMinimalQueueCapacity = 1;
    const std::uint64_t minimalPeak = tileStreamPeakBytes( request, kMinimalQueueCapacity );

    if ( minimalPeak <= request.budgetBytes )
    {
        plan.action = TileMemoryPlan::Action::ReduceConcurrency;
        plan.estimatedPeakBytes = minimalPeak;
        plan.recommendedQueueCapacity = kMinimalQueueCapacity;
        plan.reason = "tile stream at queueCapacity=" + std::to_string( requestedCap )
                      + " needs " + std::to_string( plan.requestedPeakBytes )
                      + " B, budget " + std::to_string( request.budgetBytes )
                      + " B; reduced to queueCapacity=1 ("
                      + std::to_string( minimalPeak ) + " B)";
        return plan;
    }

    if ( request.allowSpill )
    {
        const std::uint64_t bufferWidth =
            static_cast<std::uint64_t>( request.tileWidth ) + 2ull * request.haloPixels;
        const std::uint64_t bufferHeight =
            static_cast<std::uint64_t>( request.tileHeight ) + 2ull * request.haloPixels;
        const std::uint64_t perTile = saturatingMul(
            saturatingMul( saturatingMul( bufferWidth, bufferHeight ), request.bands ),
            request.bytesPerSample );
        const std::uint64_t tiles =
            request.expectedTileCount == 0
                ? 1
                : request.expectedTileCount;
        const std::uint64_t spillNeed = saturatingMul( perTile, tiles );
        if ( spillNeed <= request.scratchBudgetBytes )
        {
            plan.action = TileMemoryPlan::Action::Spill;
            plan.estimatedPeakBytes = minimalPeak;
            plan.recommendedQueueCapacity = kMinimalQueueCapacity;
            plan.spillBytes = request.expectedTileCount == 0 ? perTile : spillNeed;
            plan.reason = "tile stream minimum in-RAM shape ("
                          + std::to_string( minimalPeak ) + " B) exceeds budget "
                          + std::to_string( request.budgetBytes )
                          + " B; spilling tile intermediates to scratch ("
                          + std::to_string( plan.spillBytes )
                          + ( request.expectedTileCount == 0 ? " B/tile)" : " B)" );
            return plan;
        }
    }

    plan.action = TileMemoryPlan::Action::Refuse;
    plan.estimatedPeakBytes = minimalPeak;
    plan.recommendedQueueCapacity = kMinimalQueueCapacity;
    plan.reason = "tile working set cannot fit: minimum stream needs "
                  + std::to_string( minimalPeak ) + " B (tile "
                  + std::to_string( request.tileWidth ) + "x"
                  + std::to_string( request.tileHeight ) + " halo "
                  + std::to_string( request.haloPixels ) + " bands "
                  + std::to_string( request.bands ) + "), budget "
                  + std::to_string( request.budgetBytes ) + " B, scratch "
                  + std::to_string( request.scratchBudgetBytes )
                  + " B; reduce the tile size / halo, or raise the RAM or "
                    "scratch budget";
    return plan;
}

} // namespace sicnu::runtime::chunk
