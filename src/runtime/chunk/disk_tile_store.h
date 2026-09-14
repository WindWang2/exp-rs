// disk_tile_store.h — Scratch-backed tile payloads with a bounded write
// gate (LSEE 10.0, ADR 0148 §4).
//
// DiskTileStore serializes a TilePayload onto a ScratchLease with a
// self-describing header (magic/version/geometry/payload bytes/digest) and
// reads it back fail-closed: a truncated, magic-mismatched, or
// digest-diverging file is a typed ChunkCorruptTile, never garbage pixels
// silently entering a stream.
//
// BoundedWriteGate is the writer-throttling half of backpressure: writers
// acquire the byte amount they are about to put in flight; the gate blocks
// while outstanding bytes exceed the cap, so N concurrent writers cannot
// outrun the disk by buffering everything in page cache / RAM.
#pragma once

#include "chunk_pipeline.h"
#include "scratch_registry.h"
#include "tile_spec.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>

namespace sicnu::runtime::chunk
{

/// Raised when a tile file fails header/digest validation on read.
struct ChunkCorruptTile : std::runtime_error
{
    explicit ChunkCorruptTile( const std::string &path )
        : std::runtime_error( "corrupt scratch tile: " + path )
    {
    }
};

/// Serializes tile payloads onto scratch leases.
class DiskTileStore
{
  public:
    /// Writes @p payload's pixels to the lease's provisional path, seals the
    /// digest and finalizes the lease. Throws std::runtime_error on I/O
    /// failure (the lease stays provisional and dies with its release).
    static void write( const ScratchLease &lease, const TilePayload &payload );

    /// Reads back a finalized tile. Throws ChunkCorruptTile on any
    /// header/digest/size mismatch. The returned payload carries the spec
    /// stored in the header (the caller's expected geometry can be compared
    /// against it).
    static TilePayload read( const ScratchLease &lease );

    /// Reads a tile that may not be finalized yet (`.part` — mid-stream
    /// spill between producer and consumer in the same run). Prefer read()
    /// for restart/reuse paths.
    static TilePayload readProvisional( const ScratchLease &lease );
};

/// In-flight write-bytes limiter (disk writer backpressure).
class BoundedWriteGate
{
  public:
    /// @p maxInFlightBytes == 0 disables the gate (tests / unbounded hosts).
    explicit BoundedWriteGate( std::uint64_t maxInFlightBytes )
        : m_cap( maxInFlightBytes )
    {
    }

    /// Blocks while outstanding + @p bytes exceed the cap. A @p bytes larger
    /// than the whole cap is admitted when the gate is idle (never-starve:
    /// one oversized write must always be able to proceed).
    void acquire( std::uint64_t bytes )
    {
        std::unique_lock<std::mutex> lock( m_mutex );
        m_condition.wait( lock, [this, bytes] {
            return m_cap == 0 || m_outstanding == 0 || m_outstanding + bytes <= m_cap;
        } );
        m_outstanding += bytes;
    }

    void release( std::uint64_t bytes )
    {
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_outstanding = bytes >= m_outstanding ? 0 : m_outstanding - bytes;
        }
        m_condition.notify_all();
    }

    /// Scoped guard: acquire(bytes) on construction, release on destruction.
    struct Reservation
    {
        explicit Reservation( BoundedWriteGate &gate, std::uint64_t bytes )
            : m_gate( gate ), m_bytes( bytes )
        {
            m_gate.acquire( m_bytes );
        }
        ~Reservation() { m_gate.release( m_bytes ); }
        Reservation( const Reservation & ) = delete;
        Reservation &operator=( const Reservation & ) = delete;

      private:
        BoundedWriteGate &m_gate;
        std::uint64_t m_bytes;
    };

    std::uint64_t outstandingBytes() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_outstanding;
    }

  private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::uint64_t m_cap = 0;
    std::uint64_t m_outstanding = 0;
};

} // namespace sicnu::runtime::chunk
