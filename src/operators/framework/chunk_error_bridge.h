// chunk_error_bridge.h — the typed bridge between the chunk runtime's
// exception family and the RSOperatorError envelope (Execution Runtime
// Convergence 11.0, WP-B).
//
// Before this bridge the two error systems were disjoint: chunk runners threw
// ChunkCancelled / ChunkPartitionMismatch / ChunkCorruptTile /
// ScratchBudgetExceeded and every seam hand-rolled (or swallowed) the
// translation. The mapping below is THE contract — known-answer tested in
// tests/test_chunk_contract_11.cpp:
//
//   ChunkCancelled (incl. ChunkConsumerAborted / ChunkGraphCancelled)
//       → ErrorCode::Cancelled
//   ScratchBudgetExceeded  → ErrorCode::ResourceBudgetExceeded (4103)
//   ChunkCorruptTile       → ErrorCode::CorruptArtifactData (2005)
//   ChunkPartitionMismatch → ErrorCode::ComputationError
//   anything else          → rethrown untouched (foreign errors are not
//                            disguised; the caller keeps its own semantics)
//
// Also hosts ChunkCancelBridge: wires an RSOperatorContext's cancellation
// (flag OR callback) into the chunk runtime's poll points — the same
// between-tiles granularity the operator body itself uses.
#pragma once

#include "rs_operator_context.h"
#include "rs_operator_error.h"
#include "runtime/chunk/chunk_graph.h"
#include "runtime/chunk/chunk_pipeline.h"
#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/scratch_registry.h"
#include "runtime/exec/execution_governor.h"

#include <atomic>
#include <string>

namespace sicnu::operators
{

/// Runs @p body inside the chunk→operator error translation. The return type
/// is deduced from the body; the body's exceptions outside the chunk family
/// pass through unchanged.
template <typename Body>
auto runWithChunkErrorTranslation( Body &&body ) -> decltype( body() )
{
    try
    {
        return body();
    }
    catch ( const sicnu::runtime::chunk::ScratchBudgetExceeded &e )
    {
        throw RSOperatorError( ErrorCode::ResourceBudgetExceeded, std::string( e.what() ) );
    }
    catch ( const sicnu::runtime::exec::AdmissionRefused &e )
    {
        throw RSOperatorError( ErrorCode::ResourceBudgetExceeded, std::string( e.what() ) );
    }
    catch ( const sicnu::runtime::chunk::ChunkCorruptTile &e )
    {
        throw RSOperatorError( ErrorCode::CorruptArtifactData, std::string( e.what() ) );
    }
    catch ( const sicnu::runtime::chunk::ChunkPartitionMismatch &e )
    {
        Json::Value details( Json::objectValue );
        details["chunkError"] = "partition-mismatch";
        throw RSOperatorError( ErrorCode::ComputationError, std::string( e.what() ), details );
    }
    catch ( const sicnu::runtime::chunk::ChunkCancelled &e )
    {
        // Covers ChunkConsumerAborted and ChunkGraphCancelled (subtypes).
        throw RSOperatorError( ErrorCode::Cancelled, std::string( e.what() ) );
    }
}

/// Wires RSOperatorContext cancellation into a chunk runner. The runner polls
/// the context's OWN flag between tiles when one exists (no mirror thread, no
/// drift); callback-based contexts are covered by polling
/// throwIfCancelled() at the seam's own loop points.
class ChunkCancelBridge
{
  public:
    explicit ChunkCancelBridge( const RSOperatorContext &context ) : m_context( context ) {}

    /// Lets a pipeline/graph poll the context's flag directly. Safe no-op
    /// when the context uses a cancel callback instead.
    void wire( sicnu::runtime::chunk::ChunkPipeline &pipeline ) const
    {
        if ( const std::atomic<bool> *flag = m_context.get().cancelFlag() )
            pipeline.setCancelFlag( flag );
    }
    void wire( sicnu::runtime::chunk::ChunkGraph &graph ) const
    {
        if ( const std::atomic<bool> *flag = m_context.get().cancelFlag() )
            graph.setCancelFlag( flag );
    }

    /// Throws the runtime's cancellation exception when the context is
    /// cancelled; call at loop points of hand-rolled seams (producer/stage/
    /// consumer bodies) so callback-based contexts stop at the same
    /// between-tiles granularity.
    void throwIfCancelled() const
    {
        if ( m_context.get().isCancelled() )
            throw sicnu::runtime::chunk::ChunkCancelled();
    }

  private:
    std::reference_wrapper<const RSOperatorContext> m_context;
};

} // namespace sicnu::operators
