// sha256.h — self-contained SHA-256 for the sample data foundry (D1).
//
// The foundry must stay a standalone, Qt-free binary (DECISIONS D-010), so it
// carries its own implementation instead of linking src/geospatial. Semantics
// match the platform's other fingerprints: lowercase 64-char hex.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sicnu::foundry
{

/// Lowercase hex (64 chars) SHA-256 over the byte range [data, data + size).
std::string sha256Hex( const void *data, std::size_t size );

} // namespace sicnu::foundry
