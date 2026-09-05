// chunk_pipeline.cpp — see chunk_pipeline.h for the contract.
#include "chunk_pipeline.h"

namespace sicnu::runtime::chunk
{
namespace
{
void validateBuffer( const TilePayload &p, const char *where )
{
    const size_t expected = p.spec.bufferElementCount();
    const size_t actual = p.pixels ? p.pixels->size() : 0;
    if ( actual != expected )
        throw std::logic_error( std::string( where ) + ": tile buffer size " +
                                std::to_string( actual ) + " != spec size " +
                                std::to_string( expected ) );
}
} // namespace

ChunkPipeline::ChunkPipeline( ProducerFn producer, std::vector<StageFn> stages,
                              ConsumerFn consumer )
    : ChunkPipeline( std::move( producer ), std::move( stages ), std::move( consumer ),
                     Config{} )
{
}

ChunkPipeline::ChunkPipeline( ProducerFn producer, std::vector<StageFn> stages,
                              ConsumerFn consumer, Config config )
    : m_producer( std::move( producer ) ),
      m_stages( std::move( stages ) ),
      m_consumer( std::move( consumer ) ),
      m_config( config )
{
}

void ChunkPipeline::run()
{
    const size_t queueCount = m_stages.size() + 1;
    std::vector<std::unique_ptr<BoundedChunkQueue<TilePayload>>> queues;
    queues.reserve( queueCount );
    for ( size_t i = 0; i < queueCount; ++i )
        queues.push_back( std::make_unique<BoundedChunkQueue<TilePayload>>( m_config.queueCapacity ) );

    auto cancelAll = [this, &queues]() {
        for ( auto &q : queues )
            q->cancel();
    };
    auto cancelled = [this]() { return m_cancelFlag && m_cancelFlag->load(); };

    // Thread body contract: on exception store first error + cancelAll so no
    // peer can deadlock on a dead stage; on user cancel cancelAll + exit.
    auto threadBody = [&]( auto &&loopBody ) {
        try
        {
            loopBody();
        }
        catch ( ... )
        {
            std::lock_guard<std::mutex> lock( m_errorMutex );
            if ( !m_firstError )
                m_firstError = std::current_exception();
            cancelAll();
        }
    };

    std::vector<std::thread> threads;
    threads.reserve( m_stages.size() + 2 );

    // Producer thread.
    threads.emplace_back( [&] {
        threadBody( [&] {
            while ( !cancelled() )
            {
                TilePayload p;
                if ( !m_producer( p ) )
                    break;
                validateBuffer( p, "chunk producer" );
                if ( !queues.front()->push( std::move( p ) ) )
                    return; // downstream died or cancelled
            }
            queues.front()->close();
        } );
    } );

    // Stage threads.
    for ( size_t i = 0; i < m_stages.size(); ++i )
    {
        threads.emplace_back( [&, i] {
            threadBody( [&] {
                BoundedChunkQueue<TilePayload> &in = *queues[i];
                BoundedChunkQueue<TilePayload> &out = *queues[i + 1];
                TilePayload p;
                while ( in.pop( p ) )
                {
                    if ( cancelled() )
                    {
                        cancelAll();
                        return;
                    }
                    TilePayload result = m_stages[i]( std::move( p ) );
                    p = TilePayload{}; // release consumed buffers promptly
                    if ( result.pixels )
                    {
                        validateBuffer( result, "chunk stage" );
                        if ( !out.push( std::move( result ) ) )
                            return; // downstream died or cancelled
                    }
                    // Empty result: tile filtered out; nothing forwarded.
                }
                // in is closed and drained: close downstream (cascade).
                out.close();
            } );
        } );
    }

    // Consumer thread.
    threads.emplace_back( [&] {
        threadBody( [&] {
            BoundedChunkQueue<TilePayload> &in = *queues.back();
            TilePayload p;
            while ( in.pop( p ) )
            {
                if ( cancelled() )
                {
                    cancelAll();
                    return;
                }
                const int total = p.spec.totalTiles;
                const bool keepGoing = m_consumer( std::move( p ) );
                p = TilePayload{};
                const size_t done = m_completedTiles.fetch_add( 1 ) + 1;
                if ( m_progress && total > 0 )
                    m_progress( static_cast<double>( done ) / total );
                if ( !keepGoing )
                {
                    cancelAll();
                    return;
                }
            }
        } );
    } );

    for ( auto &t : threads )
        t.join();

    {
        std::lock_guard<std::mutex> lock( m_errorMutex );
        if ( m_firstError )
            std::rethrow_exception( m_firstError );
    }
    if ( cancelled() )
        throw ChunkCancelled();
}

} // namespace sicnu::runtime::chunk
