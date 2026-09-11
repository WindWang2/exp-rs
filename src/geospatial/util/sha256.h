/***************************************************************************
  geospatial/util/sha256.h
  Cloud-Native Geospatial Data Fabric 8.0 — self-contained SHA-256 (FIPS
  180-4) for Qt-free identity/digest work.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Why a local implementation: the identity token (remote_identity_token.h)
  needs a stable cryptographic digest inside the Qt-free geospatial library.
  GDAL ships the code but not a public header (cpl_sha256.h is an internal
  port header), Qt is off-limits here, and vendoring another library for one
  primitive is not justified. Classic public-domain-shape implementation,
  validated against the FIPS 180-4 known-answer vectors in test_io_identity.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_SHA256_H
#define SICNU_GEOSPATIAL_SHA256_H

#include "geospatial/common.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// Streaming SHA-256. Feed with update(), finish with finalize(); one-shot
/// helpers below cover the common cases. Not thread-safe per instance —
/// use one instance per thread.
class Sha256
{
  public:
    Sha256();

    void update( const void *data, std::size_t length );
    void update( const std::string &text ) { update( text.data(), text.size() ); }

    /// Completes the hash and returns the 32 raw bytes. The instance resets
    /// to the initial state afterwards (reusable).
    std::vector<unsigned char> finalize();

  private:
    void processBlock( const unsigned char *block );
    std::uint32_t mState[8];
    unsigned char mBuffer[64];
    std::uint64_t mTotalBytes;
    std::size_t mBufferUsed;
};

/// One-shot SHA-256 of a byte range — 32 raw bytes.
std::vector<unsigned char> sha256Bytes( const void *data, std::size_t length );

/// One-shot SHA-256 of a string — lowercase hex (64 chars).
std::string sha256Hex( const std::string &text );

/// Lowercase hex of raw bytes.
std::string toHex( const std::vector<unsigned char> &bytes );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_SHA256_H
