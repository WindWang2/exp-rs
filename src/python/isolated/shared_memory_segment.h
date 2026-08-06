// src/python/isolated/shared_memory_segment.h
//
// ADR 0064 - RAII shared-memory segment for zero-copy C++ <-> Python data
// exchange. Layout: [Header][payload]. The header carries dimensions and dtype
// so the Python side can mount the payload with numpy.frombuffer /
// multiprocessing.shared_memory without any extra negotiation.
//
// Synchronization contract (ADR 0064): access to a single segment is
// serialized by the `shm.read` request/response boundary. C++ creates the
// segment, writes the payload, then sends the key in a synchronous `shm.read`
// JSON-RPC request; the Python worker mounts the buffer during that request
// and shm.close()s before replying. Because every create() mints a unique
// key, concurrent reads always target *distinct* segments, so there is never
// concurrent read/write on the same buffer. No in-segment lock is needed —
// and adding one would force Python to copy/retry, defeating the zero-copy
// mount that is the point of this channel.
//
// Lifetime (ADR 0064, leak fix): the POSIX shm object backing a created
// segment persists in /dev/shm until something unlinks it. The creator owns
// that responsibility: detach() unlinks a segment this object created (and no
// longer needs), and the Python reader also best-effort unlinks on its side,
// so the object is reclaimed regardless of which side finishes last. The
// helper unlink() exposes the operation for callers that want to release the
// backing object while keeping the in-process mapping a little longer.
#pragma once

#include <QString>
#include <memory>

class QSharedMemory;

namespace sicnu::python::isolated {

class SharedMemorySegment
{
  public:
    enum class DType : int32_t { Float32 = 0, UInt8 = 1, Int32 = 2 };

    /// Fixed-layout header prepended to the payload. Must stay 32 bytes so the
    /// Python side can skip it with a hardcoded offset.
    struct Header
    {
        char uuid[16];   // unique segment id (first 16 chars of the key)
        int32_t width;
        int32_t height;
        int32_t bands;
        int32_t dtype;   // DType enum value
    };

    SharedMemorySegment();
    ~SharedMemorySegment();

    SharedMemorySegment( const SharedMemorySegment & ) = delete;
    SharedMemorySegment &operator=( const SharedMemorySegment & ) = delete;

    /// Create a new segment of the given dimensions. Returns false on failure.
    bool create( int width, int height, int bands, DType dtype );

    /// Attach to an existing segment by key (Python -> C++ result path).
    bool attach( const QString &key );
    void detach();

    /// Unlink the POSIX shm object backing this segment's key (Linux/macOS:
    /// shm_unlink; no-op on Windows). Only meaningful for a segment this
    /// object created (see isOwner()). Safe to call after detach(). Returns
    /// false if there is no native key or shm_unlink fails (e.g. already gone).
    bool unlink();

    /// True when this object created the segment (and therefore owns the
    /// responsibility to reclaim its POSIX shm object via unlink/detach).
    bool isOwner() const;

    /// Write payload data into the segment (after create). Returns false if
    /// the segment is not created or \a bytes exceeds the payload capacity.
    bool write( const void *data, size_t bytes );

    /// Read-only pointer to the payload (after the Header).
    const void *payload() const;
    void *payload();

    /// The QSharedMemory key (passed to the Python side so it can attach).
    QString key() const;

    /// The native POSIX shm name (what shm_open actually uses on Linux).
    /// On Linux this may differ from key() by a leading '/'. The Python side
    /// (multiprocessing.shared_memory) needs this native name.
    QString nativeKey() const;

    /// Payload size in bytes (excluding the Header).
    size_t payloadSize() const;

    bool isAttached() const;

    /// Bytes per element for a DType.
    static size_t dtypeSize( DType dtype );

  private:
    std::unique_ptr<QSharedMemory> m_shm;
    QString m_key;
    size_t m_payloadSize = 0;
    bool m_isOwner = false; ///< True when create() made this segment.
    bool m_unlinked = false; ///< True once unlink() has reclaimed the object.
};

} // namespace sicnu::python::isolated
