// src/python/isolated/shared_memory_segment.h
#pragma once

#include <cstdint>
#include <QString>

namespace sicnu::python::isolated
{

#pragma pack(push, 1)
struct SharedHeader
{
  char uuid[36];
  int32_t width = 0;
  int32_t height = 0;
  int32_t bands = 0;
  int32_t dataType = 0; // 0: float32, 1: uint8, 2: int16, 3: int32, 4: float64
  int32_t refCount = 1;
  uint64_t dataSize = 0;
};
#pragma pack(pop)

class SharedMemorySegment
{
  public:
    SharedMemorySegment();
    ~SharedMemorySegment();

    bool create( const QString &key, size_t payloadSize, int width = 0, int height = 0, int bands = 1, int dataType = 0 );
    bool attach( const QString &key );
    void detach();

    bool isAttached() const;
    QString key() const;
    void *payload();
    const void *payload() const;
    SharedHeader *header();
    const SharedHeader *header() const;
    size_t payloadSize() const;

  private:
    QString m_key;
    int m_fd = -1;
    void *m_mappedAddress = nullptr;
    size_t m_totalSize = 0;
};

} // namespace sicnu::python::isolated
