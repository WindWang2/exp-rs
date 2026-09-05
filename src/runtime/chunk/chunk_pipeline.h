// chunk_pipeline.h — Streaming tile pipeline runner (Data Plane 3.0, Phase B).
//
// Executes a producer → [stage]* → consumer chain as concurrent threads joined
// by BoundedChunkQueues:
//
//   producer ──queue── stage1 ──queue── … ──queue── consumer
//
// Memory is bounded by (stages+1) * queueCapacity * tileBytes, NOT by raster
// size. Contracts:
//
//  - TilePayload carries the tile geometry plus an owned float buffer. Stage
//    functions receive the payload and may return a NEW payload (different
//    buffer, e.g. band-count or halo shrink changes); returning {} skips the
//    tile downstream (filtered).
//  - Halo: the producer fills buffers with halo data; a stage that shrinks
//    the halo (e.g. a stencil kernel consuming radius k) returns a payload
//    whose spec.halo reflects ITS OUTPUT halo. The pipeline validates only
//    that buffer sizes match the returned spec.
//  - Failure propagation: any stage throwing closes/cancels every queue and
//    the run() call rethrows the first error after joining. Cancellation via
//    the Context cancel flag ends all stages with ChunkCancelled.
//  - Progress: every payload that LEAVES the final queue bumps completedTiles;
//    the progress callback receives completedTiles/totalTiles (0..1).
//  - Exactly-once ordering per queue (FIFO), so row-major order is preserved
//    through every stage when capacity >= 1 (per-queue FIFO invariant).
#pragma once

#include "bounded_chunk_queue.h"
#include "tile_spec.h"

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sicnu::runtime::chunk
{

/// Owned tile payload handed between pipeline stages.
struct TilePayload
{
    TileSpec spec;
    std::shared_ptr<std::vector<float>> pixels; ///< bufferWidth*bufferHeight*bands

    TilePayload() = default;
    TilePayload( TileSpec s, std::shared_ptr<std::vector<float>> buf )
        : spec( s ), pixels( std::move( buf ) )
    {
    }
};

/// Raised by the runner on cooperative cancellation (not by stages).
struct ChunkCancelled : std::runtime_error
{
    explicit ChunkCancelled() : std::runtime_error( "chunk pipeline cancelled" ) {}
};

class ChunkPipeline
{
  public:
    using ProducerFn = std::function<bool( TilePayload &out )>;  ///< false = end of stream
    using StageFn = std::function<TilePayload( TilePayload && )>; ///< {} = drop tile
    using ConsumerFn = std::function<bool( TilePayload && )>;    ///< false = abort (cancelled)
    using ProgressFn = std::function<void( double )>;            ///< 0..1

    struct Config
    {
        size_t queueCapacity = 2; ///< per-queue bound; 2 keeps a stage busy while
                                  ///< its downstream stage processes one tile
    };

    ChunkPipeline( ProducerFn producer, std::vector<StageFn> stages, ConsumerFn consumer );
    ChunkPipeline( ProducerFn producer, std::vector<StageFn> stages, ConsumerFn consumer,
                   Config config );

    /// Sets a cooperative cancel flag polled between tiles by every stage.
    void setCancelFlag( const std::atomic<bool> *flag ) { m_cancelFlag = flag; }
    void setProgressCallback( ProgressFn cb ) { m_progress = std::move( cb ); }

    /// Runs the pipeline to completion. Throws the first stage error
    /// (rethrows std::exception_ptr rethrowably), ChunkCancelled on cancel,
    /// or returns normally when the consumer finished the stream.
    void run();

    /// Tiles that left the final queue (for tests / diagnostics).
    size_t completedTiles() const { return m_completedTiles.load(); }

  private:
    void threadLoop( size_t stageIndex, BoundedChunkQueue<TilePayload> &in,
                     BoundedChunkQueue<TilePayload> *out );

    ProducerFn m_producer;
    std::vector<StageFn> m_stages;
    ConsumerFn m_consumer;
    Config m_config;
    const std::atomic<bool> *m_cancelFlag = nullptr;
    ProgressFn m_progress;
    std::atomic<size_t> m_completedTiles{ 0 };
    std::exception_ptr m_firstError;
    std::mutex m_errorMutex;
};

} // namespace sicnu::runtime::chunk
