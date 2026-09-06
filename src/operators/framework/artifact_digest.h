// src/operators/framework/artifact_digest.h — single owner of artifact hashing.
//
// Both the ModelCatalog (checksum verification) and the model runtime layer
// (session identity) need "the SHA-256 of this weight file's bytes". One
// utility, two consumers, so a digest can never diverge between what the
// catalog verified and what the session cache keys on.
#pragma once

#include <string>

namespace sicnu::operators {

/**
 * SHA-256 hex digest (lowercase, 64 chars) of a file's bytes, streamed in
 * 1 MiB chunks. Returns "" when the file cannot be opened or read.
 */
std::string artifactSha256Hex( const std::string &path );

/// True when @p digest is a lowercase/uppercase-insensitive 64-char hex string.
bool isSha256Hex( const std::string &digest );

} // namespace sicnu::operators
