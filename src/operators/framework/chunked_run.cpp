// chunked_run.cpp — see chunked_run.h for the adoption-kit contract.
#include "chunked_run.h"

#include "chunk_error_bridge.h"
#include "rs_operator_error.h"

#include "runtime/chunk/memory_planner.h"
#include "runtime/exec/execution_governor.h"

#include <json/json.h>
#include <json/writer.h>

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace sicnu::operators
{

using namespace sicnu::runtime::chunk;

namespace
{

std::uint64_t hashBytes( const std::string &s )
{
    return tileCheckpointInputIdentity( s );
}

/// Deterministic identity inputs: the operator's stable id + its canonical
/// parameter JSON (adapters keep params canonical; kernel-version changes
/// belong IN the params so implementations drift the identity).
std::uint64_t operatorIdentityOf( const std::string &operatorId )
{
    return hashBytes( "op:" + operatorId );
}

std::uint64_t inputIdentityOf( const Json::Value &canonicalParams )
{
    Json::FastWriter writer; // fixed formatting: same value ⇒ same bytes
    return hashBytes( writer.write( canonicalParams ) );
}

TileRunSpec makeSpec( const std::string &operatorId, const Json::Value &canonicalParams,
                      const TileRunPartition &partition, TileRunDeterminism determinism )
{
    TileRunSpec spec;
    spec.identity.operatorIdentity = operatorIdentityOf( operatorId );
    spec.identity.inputIdentity = inputIdentityOf( canonicalParams );
    spec.identity.partitionDigest = tileRunPartitionDigest( partition );
    spec.partition = partition;
    spec.determinism = determinism;
    return spec;
}

void admitThroughGovernor( const ChunkedRunOptions &options, const TileRunPartition &partition,
                           std::uint32_t queueCapacity )
{
    if ( options.ramBudgetBytes == 0 )
        return;
    // Pure planning (no filesystem side effects): the planner IS the gate;
    // the governor object exists for hosts that also need its enforced
    // scratch/write budgets, not for this decision.
    TileMemoryRequest request;
    request.tileWidth = static_cast<std::uint32_t>( partition.tileWidth );
    request.tileHeight = static_cast<std::uint32_t>( partition.tileHeight );
    request.haloPixels = static_cast<std::uint32_t>( partition.halo );
    request.bands = static_cast<std::uint32_t>( partition.bands );
    request.stageCount = 0;
    request.requestedQueueCapacity = queueCapacity;
    request.budgetBytes = options.ramBudgetBytes;
    const TileMemoryPlan plan = planTileMemory( request );
    if ( plan.action == TileMemoryPlan::Action::Refuse )
        throw sicnu::runtime::exec::AdmissionRefused( plan.reason );
}

ChunkedRunResult runResumable( const TileRunSpec &spec, RSOperatorContext &context,
                               const ChunkTileKernel &kernel,
                               const std::function<void( const TilePayload & )> &sink,
                               const ChunkedRunOptions &options )
{
    std::filesystem::path workDir = std::filesystem::path( context.workDir().empty()
                                                               ? std::filesystem::temp_directory_path()
                                                               : context.workDir() );
    const std::string scratchRoot =
        options.scratchRoot.empty() ? workDir.generic_string() : options.scratchRoot;
    const std::string stateBase =
        options.resumeStateBase.empty()
            ? ( workDir / ( "chunked-" + tileRunIdentityKey( spec.identity ) ) ).generic_string()
            : options.resumeStateBase;

    ResumableTileRun::Config cfg;
    cfg.scratchRoot = scratchRoot;
    cfg.statePath = stateBase;
    cfg.checkpointIntervalTiles = options.checkpointIntervalTiles;

    ResumableTileRun run( spec, cfg );

    TileRunCancelSource cancel;
    cancel.flag = context.cancelFlag();
    cancel.predicate = [&context] { return context.isCancelled(); };

    std::uint64_t done = 0;
    const std::uint64_t total = spec.partition.totalTiles();

    ResumableTileRun::Callbacks cb;
    cb.compute = [&kernel]( const TileSpec &tileSpec ) {
        std::vector<float> buffer = kernel( tileSpec );
        auto owned = std::make_shared<std::vector<float>>( std::move( buffer ) );
        return TilePayload{ tileSpec, std::move( owned ) };
    };
    cb.consume = [&]( const TilePayload &payload ) {
        sink( payload );
        ++done;
        context.reportProgress( static_cast<double>( done ) / static_cast<double>( total ),
                                "chunked run" );
    };
    cb.publish = options.publish ? options.publish : [] {};

    ChunkedRunResult result;
    runWithChunkErrorTranslation( [&] {
        const auto runResult = run.execute( cancel, cb );
        result.totalTiles = runResult.totalTiles;
        result.tilesComputed = runResult.tilesComputed;
        result.tilesReused = runResult.tilesReused;
        result.alreadyPublished = runResult.alreadyPublished;
        return 0;
    } );
    return result;
}

ChunkedRunResult runPipelineMode( const TileRunSpec &spec, RSOperatorContext &context,
                                  const ChunkTileKernel &kernel,
                                  const std::function<void( const TilePayload & )> &sink,
                                  const ChunkedRunOptions &options )
{
    const ChunkCancelBridge bridge( context );
    const std::uint64_t total = spec.partition.totalTiles();

    // Cancellation bridges BOTH context shapes: the pipeline polls the
    // context's own flag when one exists, and the bodies poll
    // throwIfCancelled() so callback-based contexts stop at the same
    // between-tiles granularity (same pattern as fused_chain).
    auto producer = [&kernel, &spec, &bridge, total,
                     next = std::uint64_t { 0 }]( TilePayload &out ) mutable {
        bridge.throwIfCancelled();
        if ( next >= total )
            return false;
        const TileSpec tileSpec = tileSpecAt( spec.partition, next++ );
        auto buffer = std::make_shared<std::vector<float>>( kernel( tileSpec ) );
        out = TilePayload{ tileSpec, std::move( buffer ) };
        return true;
    };

    ChunkPipeline::Config cfg;
    cfg.queueCapacity = options.pipelineQueueCapacity;
    ChunkPipeline pipeline( producer, {}, [&bridge, &sink]( TilePayload &&p ) {
        bridge.throwIfCancelled();
        sink( p );
        return true;
    }, cfg );
    bridge.wire( pipeline );

    pipeline.setProgressCallback( [&context]( double fraction ) {
        context.reportProgress( fraction, "chunked run" );
    } );

    runWithChunkErrorTranslation( [&pipeline] {
        pipeline.run();
        return 0;
    } );

    ChunkedRunResult result;
    result.totalTiles = total;
    result.tilesComputed = total;
    return result;
}

} // namespace

ChunkedRunResult runChunkedOperator(
    const std::string &operatorId, const Json::Value &canonicalParams,
    RSOperatorContext &context, const TileRunPartition &partition,
    TileRunDeterminism determinism, const ChunkTileKernel &kernel,
    const std::function<void( const TilePayload & )> &sink, const ChunkedRunOptions &options )
{
    if ( operatorId.empty() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "chunked run: operatorId is required" );
    if ( !kernel || !sink )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "chunked run: kernel and sink are required" );

    const TileRunSpec spec = makeSpec( operatorId, canonicalParams, partition, determinism );

    // Hard admission first (fail before any work): AdmissionRefused maps to
    // ResourceBudgetExceeded through the chunk error bridge below.
    runWithChunkErrorTranslation( [&] {
        admitThroughGovernor( options, partition,
                              options.mode == ChunkedRunOptions::Mode::Pipeline
                                  ? options.pipelineQueueCapacity
                                  : 1 );
        return 0;
    } );

    if ( options.mode == ChunkedRunOptions::Mode::Pipeline )
        return runPipelineMode( spec, context, kernel, sink, options );
    return runResumable( spec, context, kernel, sink, options );
}

} // namespace sicnu::operators
