/***************************************************************************
  lab/sha256.h
  LabSpec v3 lab runtime — self-contained SHA-256 (FIPS 180-4).

  Why a local implementation: the lab runtime is a Qt-free metadata layer
  that links jsoncpp only (the layer guard is the test link graph), so it
  can neither use QCryptographicHash nor pull Sicnu::Geospatial for
  geospatial/util/sha256.h. Same public-domain-shape discipline as the
  geospatial utility; validated against the FIPS 180-4 known-answer vectors
  in tests/test_lab_runtime.cpp. Not for adversarial crypto use — digests
  here are identity/drift evidence, not a trust root.
***************************************************************************/

#ifndef SICNU_LAB_SHA256_H
#define SICNU_LAB_SHA256_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sicnu::lab
{

/// One-shot SHA-256 over a byte range; returns 64 lowercase hex chars.
std::string sha256Hex( std::string_view bytes );

/// Incremental SHA-256 for callers that must digest a large input from a
/// bounded-memory stream (e.g. a verdict artifact) instead of buffering it
/// whole. Same FIPS 180-4 core as sha256Hex; update() may be called any
/// number of times with any chunk size, finalizeHex() seals exactly once.
class Sha256Stream
{
public:
  void update( const void *data, std::size_t size );
  std::string finalizeHex();

private:
  uint32_t m_state[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  };
  uint64_t m_totalBytes = 0;
  uint8_t m_buffer[64] = {};
  std::size_t m_buffered = 0;
};

} // namespace sicnu::lab

#endif // SICNU_LAB_SHA256_H
