// chunk_graph.cpp — Multi-input tile DAG runner (see chunk_graph.h).
#include "chunk_graph.h"

#include <cassert>
#include <utility>

namespace sicnu::runtime::chunk
{

namespace
{
/// True when the cooperative cancel flag is set (null flag = never).
bool flagCancelled( const std::atomic<bool> *flag )
{
    return flag && flag->load( std::memory_order_relaxed );
}
} // namespace

ChunkGraph::ChunkGraph( Config config ) : m_config( config ) {}

ChunkGraph::~ChunkGraph()
{
    // run() joins its threads; a graph destroyed mid-run is a caller bug —
    // detach nothing, cancel and join defensively instead.
    if ( !m_threads.empty() )
    {
        fail( std::exception_ptr() );
        for ( auto &thread : m_threads )
            if ( thread.joinable() )
                thread.join();
    }
}

ChunkGraph::NodeId ChunkGraph::addSource( SourceFn fn )
{
    Node node;
    node.kind = Node::Kind::Source;
    node.source = std::move( fn );
    node.out = std::make_shared<BoundedChunkQueue<TilePayload>>( m_config.queueCapacity );
    m_nodes.push_back( std::move( node ) );
    return static_cast<NodeId>( m_nodes.size() ) - 1;
}

ChunkGraph::NodeId ChunkGraph::addStage( NodeId input, StageFn fn )
{
    assert( input >= 0 && input < static_cast<NodeId>( m_nodes.size() ) );
    Node node;
    node.kind = Node::Kind::Stage;
    node.stage = std::move( fn );
    node.inputs = { input };
    node.out = std::make_shared<BoundedChunkQueue<TilePayload>>( m_config.queueCapacity );
    m_nodes.push_back( std::move( node ) );
    return static_cast<NodeId>( m_nodes.size() ) - 1;
}

ChunkGraph::NodeId ChunkGraph::addJoin( std::vector<NodeId> inputs, JoinFn fn )
{
    assert( !inputs.empty() );
    for ( const NodeId id : inputs )
        assert( id >= 0 && id < static_cast<NodeId>( m_nodes.size() ) );
    Node node;
    node.kind = Node::Kind::Join;
    node.join = std::move( fn );
    node.inputs = std::move( inputs );
    node.out = std::make_shared<BoundedChunkQueue<TilePayload>>( m_config.queueCapacity );
    m_nodes.push_back( std::move( node ) );
    return static_cast<NodeId>( m_nodes.size() ) - 1;
}

void ChunkGraph::addSink( NodeId input, SinkFn fn )
{
    assert( input >= 0 && input < static_cast<NodeId>( m_nodes.size() ) );
    m_sinkInput = input;
    m_sink = std::move( fn );
}

ChunkGraph::QueuePtr ChunkGraph::queueFor( NodeId id ) const
{
    assert( id >= 0 && id < static_cast<NodeId>( m_nodes.size() ) );
    return m_nodes[id].out;
}

bool ChunkGraph::isCancelling() const
{
    if ( flagCancelled( m_cancelFlag ) )
        return true;
    std::lock_guard<std::mutex> lock( m_state.mutex );
    return m_state.cancelling;
}

void ChunkGraph::fail( const std::exception_ptr &error )
{
    std::lock_guard<std::mutex> lock( m_state.mutex );
    if ( error && !m_state.firstError )
        m_state.firstError = error;
    m_state.cancelling = true;
    for ( const auto &node : m_nodes )
        if ( node.out )
            node.out->cancel();
}

bool ChunkGraph::cancelUnwind()
{
    // Cooperative cancel unwinds exactly like a failure: EVERY queue is
    // cancelled so a node parked in pop() wakes and drains, then run()
    // reports ChunkGraphCancelled. Never just close-one-queue — a join
    // waiting on a different input would stay parked (deadlock).
    std::lock_guard<std::mutex> lock( m_state.mutex );
    m_state.cancelled = true;
    m_state.cancelling = true;
    for ( const auto &node : m_nodes )
        if ( node.out )
            node.out->cancel();
    return false;
}

void ChunkGraph::runSource( Node &node )
{
    try
    {
        TilePayload payload;
        while ( !isCancelling() && node.source( payload ) )
        {
            if ( !node.out->push( std::move( payload ) ) )
                return; // graph unwinding downstream — drop the tile
            payload = TilePayload{};
            m_sourceTileCount.fetch_add( 1, std::memory_order_relaxed );
        }
        // Close on BOTH exits (EOF and cancel): downstream pop() must wake.
        node.out->close();
    }
    catch ( ... )
    {
        fail( std::current_exception() );
    }
}

void ChunkGraph::runStage( Node &node )
{
    QueuePtr in = queueFor( node.inputs.front() );
    try
    {
        TilePayload payload;
        while ( in->pop( payload ) )
        {
            if ( isCancelling() )
            {
                cancelUnwind();
                return;
            }
            TilePayload out = node.stage( std::move( payload ) );
            payload = TilePayload{};
            if ( out.pixels )
            {
                if ( out.spec.bufferElementCount() != out.pixels->size() )
                    throw std::runtime_error( "chunk graph stage buffer/spec mismatch" );
                if ( !node.out->push( std::move( out ) ) )
                    return;
            }
        }
        if ( node.out )
            node.out->close();
    }
    catch ( ... )
    {
        fail( std::current_exception() );
    }
}

void ChunkGraph::runJoin( Node &node )
{
    std::vector<QueuePtr> inputs;
    inputs.reserve( node.inputs.size() );
    for ( const NodeId id : node.inputs )
        inputs.push_back( queueFor( id ) );

    try
    {
        while ( true )
        {
            if ( isCancelling() )
            {
                cancelUnwind();
                return;
            }
            std::vector<TilePayload> tiles( inputs.size() );
            bool anyDelivered = false;
            bool anyDrained = false;
            int firstDrained = -1;
            // Pop in DECLARED order: tile k of input i aligns with tile k of
            // every other input (deterministic tuple). pop() only returns
            // false when that input is closed AND drained, so a drained
            // input beside a delivering one is a real partition breach,
            // never a race — detected in the same iteration either way.
            for ( size_t i = 0; i < inputs.size(); ++i )
            {
                if ( inputs[i]->pop( tiles[i] ) )
                {
                    anyDelivered = true;
                }
                else
                {
                    anyDrained = true;
                    if ( firstDrained < 0 )
                        firstDrained = static_cast<int>( i );
                }
            }
            if ( anyDrained && anyDelivered )
                throw ChunkPartitionMismatch( firstDrained );
            if ( anyDrained )
                break; // every input closed and drained: clean EOF
            TilePayload out = node.join( std::move( tiles ) );
            if ( out.pixels )
            {
                if ( out.spec.bufferElementCount() != out.pixels->size() )
                    throw std::runtime_error( "chunk graph join buffer/spec mismatch" );
                if ( !node.out->push( std::move( out ) ) )
                    return;
            }
        }
        node.out->close();
    }
    catch ( ... )
    {
        fail( std::current_exception() );
    }
}

void ChunkGraph::runSink( Node &node )
{
    QueuePtr in = queueFor( node.inputs.front() );
    try
    {
        TilePayload payload;
        while ( in->pop( payload ) )
        {
            m_completedTiles.fetch_add( 1, std::memory_order_relaxed );
            if ( m_progress && !isCancelling() )
            {
                const int emitted = m_sourceTileCount.load( std::memory_order_relaxed );
                if ( emitted > 0 )
                    m_progress( static_cast<double>( m_completedTiles.load() ) / emitted );
            }
            if ( !node.sink( std::move( payload ) ) )
            {
                // Consumer abort: same unwind path as a cancel.
                cancelUnwind();
                return;
            }
        }
    }
    catch ( ... )
    {
        fail( std::current_exception() );
    }
}

void ChunkGraph::throwTerminalState()
{
    std::lock_guard<std::mutex> lock( m_state.mutex );
    if ( m_state.firstError )
        std::rethrow_exception( m_state.firstError );
    if ( m_state.cancelled || flagCancelled( m_cancelFlag ) )
        throw ChunkGraphCancelled();
}

void ChunkGraph::run()
{
    assert( m_sinkInput >= 0 && m_sink );
    assert( m_threads.empty() ); // one run per graph instance

    // Sink node is materialized last so it can be replaced during build.
    Node sinkNode;
    sinkNode.kind = Node::Kind::Sink;
    sinkNode.sink = m_sink;
    sinkNode.inputs = { m_sinkInput };
    m_nodes.push_back( std::move( sinkNode ) );

    for ( size_t i = 0; i < m_nodes.size(); ++i )
    {
        Node &node = m_nodes[i];
        switch ( node.kind )
        {
        case Node::Kind::Source:
            m_threads.emplace_back( [this, &node] { runSource( node ); } );
            break;
        case Node::Kind::Stage:
            m_threads.emplace_back( [this, &node] { runStage( node ); } );
            break;
        case Node::Kind::Join:
            m_threads.emplace_back( [this, &node] { runJoin( node ); } );
            break;
        case Node::Kind::Sink:
            m_threads.emplace_back( [this, &node] { runSink( node ); } );
            break;
        }
    }

    for ( auto &thread : m_threads )
        thread.join();
    m_threads.clear();

    try
    {
        throwTerminalState();
    }
    catch ( ... )
    {
        // Leave the graph reusable for a fresh run() only via a new instance;
        // the node vector stays as built for diagnostics.
        throw;
    }
}

} // namespace sicnu::runtime::chunk
