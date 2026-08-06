// src/python/isolated/shared_memory_segment.h
//
// ADR 0064 - RAII shared-memory segment for zero-copy C++ <-> Python data
// exchange. Layout: [Header][payload]. The header carries dimensions and dtype
// so the Python side can mount the payload with numpy.frombuffer /
// multiprocessing.shared_memory without any extra negotiation.
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
};

} // namespace sicnu::python::isolated
