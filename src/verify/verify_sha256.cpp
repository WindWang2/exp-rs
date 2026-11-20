// src/verify/verify_sha256.cpp — verifier API adapter for the shared SHA-256.
#include "verify_sha256.h"

#include "geospatial/util/sha256.h"

namespace sicnu::verify
{

std::string sha256Hex( const std::uint8_t *data, std::size_t size )
{
    return sicnu::geo::toHex( sicnu::geo::sha256Bytes( data, size ) );
}

std::string sha256Hex( const std::string &bytes )
{
    return sicnu::geo::sha256Hex( bytes );
}

} // namespace sicnu::verify
