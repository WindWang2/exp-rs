// sha256.h — self-contained SHA-256 for PreflightReport digests.
//
// The preflight library is a jsoncpp-only leaf: pulling a crypto framework
// (or Qt's QCryptographicHash) would break the dependency contract, so the
// report digest uses this small implementation instead. Not for secrets —
// only tamper-evidence over report JSON.

#pragma once

#include <cstdint>
#include <string>

namespace sicnu::preflight {

/// Lowercase hex SHA-256 of `bytes` (64 characters).
std::string sha256Hex( const std::string &bytes );

/// First 16 hex characters of sha256Hex — the repo's content-fingerprint
/// width (same convention as planner plan ids / verifier digests).
std::string shortDigest( const std::string &bytes );

} // namespace sicnu::preflight
