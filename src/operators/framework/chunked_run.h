// chunked_run.h — the chunked-operator adoption kit (Execution Runtime
// Convergence 11.0, WP-G).
//
// ONE entry point that gives an existing or new operator the whole execution
// substrate without hand-rolling another tile loop:
//
//   auto result = runChunkedOperator( name(), canonicalParamsJson, context,
//                                      partition, kernel, sink, options );
//
// What the kit provides by construction (previously per-operator hand-rolled
// or missing — see docs/execution/ADOPTION_GUIDE.md):
//   - TileRun identity from the operator id + canonical params (partition
//     digest included) — resume/publication gates are correct by construction;
//   - bounded streaming via ChunkPipeline (queue-capped) OR crash-safe
//     resumable execution via ResumableTileRun (journal + checkpoint +
//     exactly-once publication marker);
//   - cancellation bridged from RSOperatorContext (flag AND callback) at
//     between-tiles granularity; progress bridged to context.reportProgress;
//   - typed error envelope: the chunk family maps onto RSOperatorError codes
//     (Cancelled / CorruptArtifactData / ResourceBudgetExceeded / ...);
//   - optional hard RAM admission through the ExecutionGovernor.
//
// Old APIs are untouched: this is purely additive. Domain operators adopt it
// incrementally (this track ships a synthetic reference adopter in
// tests/test_chunk_adoption_11.cpp, NOT a new builtin operator).
#pragma once

#include "rs_operator_context.h"
#include "runtime/chunk/chunk_pipeline.h"
#include "runtime/chunk/resumable_tile_run.h"
#include "runtime/chunk/tile_run_contract.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Json
{
class Value;
}

namespace sicnu::operators
{

/// Computes ONE tile's buffer: receives the tile spec (geometry incl. halo);
/// returns exactly spec.bufferElementCount() floats (halo included).
using ChunkTileKernel = std::function<std::vector<float>( const sicnu::runtime::chunk::TileSpec & )>;

struct ChunkedRunOptions
{
    /// Execution mode: resumable sequential (default; crash-safe, O(1 tile)
    /// memory) or bounded pipeline (streaming throughput; no resume).
    enum class Mode
    {
        Resumable,
        Pipeline
    };
    Mode mode = Mode::Resumable;

    /// Durable-state base path for resume mode. Empty → context.workDir() +
    /// "/chunked-" + run identity key. SHOULD live next to the final output
    /// (not inside a swept scratch root).
    std::string resumeStateBase;
    /// Scratch root for resumable tile files. Empty → context.workDir().
    std::string scratchRoot;
    std::uint64_t checkpointIntervalTiles = 64;
    /// Pipeline mode: per-queue capacity bound.
    std::uint32_t pipelineQueueCapacity = 2;
    /// Hard RAM admission (bytes; 0 = skip admission). Refusal surfaces as
    /// RSOperatorError(ResourceBudgetExceeded) BEFORE any tile runs.
    std::uint64_t ramBudgetBytes = 0;
    /// Optional final publication callback (resumable mode): called once all
    /// tiles were sinked; must be atomic-rebuildable (.part → rename).
    std::function<void()> publish;
};

struct ChunkedRunResult
{
    std::uint64_t totalTiles = 0;
    std::uint64_t tilesComputed = 0;
    std::uint64_t tilesReused = 0; ///< verified disk attach (resumable mode)
    bool alreadyPublished = false; ///< PUBLISHED marker hit: zero kernel work
};

/// Runs the chunked computation. @p sink receives every tile exactly once
/// per execution, in index order (from verified disk on resume). Throws
/// RSOperatorError on failure/cancellation; ChunkedRunResult on success.
ChunkedRunResult runChunkedOperator(
    const std::string &operatorId, const Json::Value &canonicalParams,
    RSOperatorContext &context, const sicnu::runtime::chunk::TileRunPartition &partition,
    sicnu::runtime::chunk::TileRunDeterminism determinism,
    const ChunkTileKernel &kernel,
    const std::function<void( const sicnu::runtime::chunk::TilePayload & )> &sink,
    const ChunkedRunOptions &options = {} );

} // namespace sicnu::operators
