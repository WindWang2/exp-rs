// bounded_chunk_queue.h — Bounded, cancellable MPMC queue for chunk pipelines.
//
// The memory bound of every chunk pipeline flows through here: at most
// `capacity` tiles may be in flight between two stages, so peak memory is
// O(capacity * tileBytes) regardless of raster size — never O(raster).
//
// Semantics:
//  - push() blocks while full; pop() blocks while empty and open.
//  - close() releases all waiters; later pops fail immediately; pushes after
//    close fail (payload dropped — the pipeline is shutting down).
//  - cancel() is close() with a flag: waiters wake and observe cancelled();
//    used by failure propagation and cooperative cancel mid-stream.
//  - A push/pop pair that observes cancellation returns false; producers must
//    NOT treat that as "consumer closed normally" — check cancelled().
//  - "Drained" = closed && empty: the standard loop is
//      while ( pop( item ) ) { ... }   // false on close/drain or cancel
#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

namespace sicnu::runtime::chunk
{

template<typename T>
class BoundedChunkQueue
{
  public:
    explicit BoundedChunkQueue( size_t capacity )
        : m_capacity( capacity < 1 ? 1 : capacity )
    {
    }

    BoundedChunkQueue( const BoundedChunkQueue & ) = delete;
    BoundedChunkQueue &operator=( const BoundedChunkQueue & ) = delete;

    /// Blocks while full. Returns false if the queue was closed/cancelled
    /// before the item could be enqueued (item left intact).
    bool push( T item )
    {
        std::unique_lock<std::mutex> lock( m_mutex );
        m_pushWait.wait( lock, [this] { return m_items.size() < m_capacity || m_closed; } );
        if ( m_closed )
            return false;
        m_items.push_back( std::move( item ) );
        lock.unlock();
        m_popWait.notify_one();
        return true;
    }

    /// Blocks while empty and open. Returns true with an item, or false when
    /// the queue is closed and drained (or cancelled).
    bool pop( T &out )
    {
        std::unique_lock<std::mutex> lock( m_mutex );
        m_popWait.wait( lock, [this] { return !m_items.empty() || m_closed; } );
        if ( m_items.empty() )
            return false; // closed && drained (covers cancelled too)
        out = std::move( m_items.front() );
        m_items.pop_front();
        lock.unlock();
        m_pushWait.notify_one();
        return true;
    }

    /// Non-blocking pop for polling patterns. Same return contract as pop().
    bool tryPop( T &out )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( m_items.empty() )
            return false;
        out = std::move( m_items.front() );
        m_items.pop_front();
        m_pushWait.notify_one();
        return true;
    }

    /// Closes the queue; blocked pushers/poppers wake. Idempotent.
    void close()
    {
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            if ( m_closed )
                return;
            m_closed = true;
        }
        m_pushWait.notify_all();
        m_popWait.notify_all();
    }

    /// close() + cancelled flag. Idempotent; a cancelled queue never reopens.
    void cancel()
    {
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_cancelled = true;
            if ( m_closed )
                return;
            m_closed = true;
        }
        m_pushWait.notify_all();
        m_popWait.notify_all();
    }

    bool isClosed() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_closed;
    }
    bool cancelled() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_cancelled;
    }
    size_t capacity() const { return m_capacity; }

  private:
    mutable std::mutex m_mutex;
    std::condition_variable m_pushWait;
    std::condition_variable m_popWait;
    std::deque<T> m_items;
    size_t m_capacity;
    bool m_closed = false;
    bool m_cancelled = false;
};

} // namespace sicnu::runtime::chunk
