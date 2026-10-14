// sha256.h — self-contained SHA-256 for PreflightReport digests.
//
// The preflight library is a jsoncpp leaf with one recorded exception: the
// report/request digest canonicalization rides the platform's RFC 8785
// authority via a public sicnu_data link (R12-E D8, see report.cpp). Pulling
// a full crypto framework (or Qt's QCryptographicHash) would still break the
// dependency contract, so the hash itself stays this small implementation —
// its output is pinned byte-equal to the verify/grader/faultlab forks by the
// parity gate in tests/test_verify_chain_locale_matrix.cpp. Not for secrets —
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
