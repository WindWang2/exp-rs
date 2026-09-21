// src/verify/verify_sha256.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — digest primitive.
//
// Self-contained SHA-256 (FIPS 180-4) so the verifier leaf library keeps
// its zero-sicnu-dependency discipline (platform convention is sha256 for
// digests: artifactFingerprint, result_fingerprint, lab grade digest).
// Validated against the RFC 6234 / NIST vectors in test_verifier_schema.
//

#include <cstdint>
#include <string>

namespace sicnu::verify
{

/// Lowercase hex SHA-256 of @p bytes.
std::string sha256Hex( const std::string &bytes );

/// Lowercase hex SHA-256 of the raw buffer.
std::string sha256Hex( const std::uint8_t *data, std::size_t size );

} // namespace sicnu::verify
