// util/sha256.h — self-contained SHA-256 (FIPS 180-4) for the fault-lab core.
//
// Why a local implementation: the fault-lab core is deliberately dependency-
// free (no Qt, no GDAL) so its semantics stay unit-testable in a small pure
// C++ binary. The repo's other SHA-256 surfaces do not fit that constraint
// (QCryptographicHash binds Qt, sicnu::geo::Sha256 lives in the GDAL-linked
// geospatial library). Classic public-domain-shape implementation, validated
// against the FIPS 180-4 known-answer vectors in test_faultlab.
#pragma once

#include <cstdint>
#include <string>

namespace sicnu::faultlab
{

/// Lowercase hex SHA-256 digest (64 chars) of a byte range.
std::string sha256Hex( const void *data, std::size_t length );

/// Lowercase hex SHA-256 digest (64 chars) of a string.
std::string sha256Hex( const std::string &text );

} // namespace sicnu::faultlab
