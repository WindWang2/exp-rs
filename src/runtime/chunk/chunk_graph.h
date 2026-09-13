// chunk_graph.h — Multi-input tile DAG runner (LSEE 10.0, ADR 0148).
//
// Generalizes the linear ChunkPipeline to a directed acyclic graph of tile
// nodes joined by BoundedChunkQueues:
//
//   source0 ──q── stage ──q──┐
//                            join ──q── sink
//   source1 ──q──────────────┘
//
// One thread runs per node; every queue is bounded, so peak memory stays
// O(Σ in-flight tiles) — never O(raster). Contracts (same family as
// ChunkPipeline / BoundedChunkQueue):
//
//  - Deterministic partition: every source of a join must emit tiles over the
//    SAME row-major grid partition (buildTileGrid), so the k-th pop from each
//    input aligns as tile k regardless of thread timing. The join ends when
//    every input is closed AND drained; an input drained while another still
//    holds data is a partition mismatch → typed ChunkPartitionMismatch, graph
//    cancelled, no partial output.
//  - Fan-in backpressure: a join pops one tile per input per output tile, so
//    a fast input parks in its bounded queue while slower inputs catch up.
//  - Failure/cancel propagation: the first node exception is captured, every
//    queue is cancelled, all threads drain, and run() rethrows after joining.
//    The external cancel flag (setCancelFlag) ends every node with
//    ChunkCancelled. No node ever waits on a queue that cannot be woken.
//  - Progress: every payload that LEAVES the final queue bumps completedTiles.
//  - Additive TileSpec identity: bandOffset / timeIndex ride in the payload
//    spec; buffer sizing is unchanged.
#pragma once

#include "bounded_chunk_queue.h"
#include "chunk_pipeline.h"
#include "tile_spec.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sicnu::runtime::chunk
{

/// Raised when a join's inputs disagree on the tile partition (an input
/// reached EOF while another still held tiles) — a producer contract breach,
/// never a scheduling artifact.
struct ChunkPartitionMismatch : std::runtime_error
{
    explicit ChunkPartitionMismatch( int inputIndex )
        : std::runtime_error( "chunk graph join partition mismatch at input "
                              + std::to_string( inputIndex ) )
    {
    }
};

/// Raised by the runner on cooperative cancellation (not by nodes). Derives
/// ChunkCancelled so callers written against the pipeline's cancellation
/// type keep catching graph cancellations too (F-A-8); the graph-specific
/// abort-to-cancel terminal semantics are documented at addSink.
struct ChunkGraphCancelled : ChunkCancelled
{
    explicit ChunkGraphCancelled() : ChunkCancelled() {}
};

class ChunkGraph
{
  public:
    using NodeId = int;
    using SourceFn = std::function<bool( TilePayload &out )>;                  ///< false = end of stream
    using StageFn = std::function<TilePayload( TilePayload && )>;              ///< {} = drop tile
    using JoinFn = std::function<TilePayload( std::vector<TilePayload> && )>;  ///< {} = drop tile
    using SinkFn = std::function<bool( TilePayload && )>;                      ///< false = abort (cancelled)
    using ProgressFn = std::function<void( double )>;                          ///< 0..1

    struct Config
    {
        size_t queueCapacity = 2; ///< per-queue bound (same rationale as ChunkPipeline)
    };

    ChunkGraph() = default;
    explicit ChunkGraph( Config config );
    ~ChunkGraph();

    ChunkGraph( const ChunkGraph & ) = delete;
    ChunkGraph &operator=( const ChunkGraph & ) = delete;

    /// Adds a tile source (one thread). @p fn produces tiles until it returns
    /// false. Tile index order defines the partition for downstream joins.
    NodeId addSource( SourceFn fn );
    /// Adds a 1-input transform after @p input. A node may have at most ONE
    /// consumer: pointing a second stage/join/sink at the same node throws
    /// std::logic_error (a shared queue would silently split tiles between
    /// consumers — fan-out must go through an explicit copy stage, F-A-10).
    NodeId addStage( NodeId input, StageFn fn );
    /// Adds an N-input join. Every input must be a live node id; inputs are
    /// popped in the ORDER GIVEN (deterministic tuple alignment). The
    /// single-consumer rule above applies to every input as well.
    NodeId addJoin( std::vector<NodeId> inputs, JoinFn fn );
    /// Sets the single consumer. Calling twice replaces the sink (tests);
    /// the previous sink's thread has not started yet — building happens
    /// strictly before run().
    void addSink( NodeId input, SinkFn fn );

    /// Cooperative cancel flag polled between tiles by every node.
    void setCancelFlag( const std::atomic<bool> *flag ) { m_cancelFlag = flag; }
    void setProgressCallback( ProgressFn cb ) { m_progress = std::move( cb ); }

    /// Runs the graph to completion: starts every node thread, joins them
    /// all, then rethrows the first node error, throws ChunkGraphCancelled
    /// (as a ChunkCancelled) on cooperative cancel or sink abort, or returns
    /// normally when the sink drained every source. Never returns with a
    /// node thread alive. Throws std::logic_error when called twice — one
    /// run per graph instance (F-A-18: a hard check, not a debug assert).
    /// NOTE the deliberate divergence from ChunkPipeline: a consumer abort
    /// IS a cancellable terminal state here, not a silent normal return —
    /// graph-level callers get an observable "stopped early" outcome.
    void run();

    /// Tiles that left the final queue (tests / diagnostics).
    size_t completedTiles() const { return m_completedTiles.load(); }

  private:
    struct Node
    {
        enum class Kind
        {
            Source,
            Stage,
            Join,
            Sink
        };
        Kind kind{};
        SourceFn source;
        StageFn stage;
        JoinFn join;
        SinkFn sink;
        std::vector<NodeId> inputs;   ///< stage/join/sink: input node ids (ordered)
        std::shared_ptr<BoundedChunkQueue<TilePayload>> out; ///< null for the sink
    };

    using QueuePtr = std::shared_ptr<BoundedChunkQueue<TilePayload>>;

    QueuePtr queueFor( NodeId id ) const; ///< nullptr for the sink
    /// Throws std::logic_error when @p input already has a consumer node
    /// (single-consumer rule, F-A-10). Build-time only.
    void requireFreeConsumerLocked( NodeId input ) const;
    /// Node bodies. Every body honors: exception capture + global cancel,
    /// close own output on finish, cancel everything on error.
    void runSource( Node &node );
    void runStage( Node &node );
    void runJoin( Node &node );
    void runSink( Node &node );
    /// Captures the first exception and cancels every queue (idempotent).
    void fail( const std::exception_ptr &error );
    /// Marks the cooperative-cancel path and cancels every queue so nodes
    /// parked in pop() wake and drain (returns false for `return cancelUnwind()`).
    bool cancelUnwind();
    /// True once the graph is unwinding (first error captured or cancel seen).
    bool isCancelling() const;

    void throwTerminalState(); ///< shared resolve of error vs cancel (m_state mutex held)

    Config m_config;
    std::vector<Node> m_nodes;
    NodeId m_sinkInput = -1;
    SinkFn m_sink;
    const std::atomic<bool> *m_cancelFlag = nullptr;
    ProgressFn m_progress;

    struct RunState
    {
        std::mutex mutex;
        std::exception_ptr firstError;
        bool cancelling = false;
        bool cancelled = false; ///< cooperative cancel observed by any node
    };
    mutable RunState m_state; ///< reset by run(); mutable: const observers lock it
    std::atomic<size_t> m_completedTiles{ 0 };
    std::atomic<int> m_sourceTileCount{ 0 }; ///< total tiles any source emitted
    std::atomic<double> m_lastProgress{ 0.0 }; ///< monotonic progress clamp (F-A-9)
    std::vector<std::thread> m_threads;
    bool m_ran = false; ///< one-run guard (F-A-18): threads are joined and
                        ///  cleared at the end of every run(), so emptiness
                        ///  cannot signal "already ran"
};

} // namespace sicnu::runtime::chunk
