// grader_sha256.h — self-contained SHA-256 (FIPS 180-4) for the grader leaf
// library. Qt-free on purpose: the grader links only jsoncpp, and the Qt-side
// canonicalizer (src/data/execution_fingerprint.h) is out of reach. Same
// trade-off the sibling leaf libraries made (see src/verify/verify_sha256.*,
// src/agentbench). Validated against RFC 6234 / NIST vectors in
// tests/test_grader_schema.cpp.
#pragma once

#include <cstdint>
#include <string>

namespace sicnu::grader {

/// Streaming SHA-256.
class Sha256
{
public:
    Sha256();
    void update( const unsigned char *data, std::size_t len );
    void update( const char *data, std::size_t len ) { update( reinterpret_cast<const unsigned char *>( data ), len ); }
    /// Finalizes and returns the digest as 64 lowercase hex characters.
    std::string hexDigest();

private:
    void transform( const unsigned char *block );
    std::uint32_t m_state[8];
    std::uint64_t m_bitLen = 0;
    unsigned char m_buffer[64];
    std::size_t m_bufferLen = 0;
    bool m_finalized = false;
};

/// One-shot convenience: lowercase hex digest of the bytes.
std::string sha256Hex( const std::string &bytes );

} // namespace sicnu::grader
