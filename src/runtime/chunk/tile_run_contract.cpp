// tile_run_contract.cpp — see tile_run_contract.h for the contract laws.
#include "tile_run_contract.h"

#include <cstdio>

namespace sicnu::runtime::chunk
{

namespace
{
/// One tagged, fixed-width field of the canonical partition encoding.
struct Field
{
    char tag;
    std::uint64_t value;
};

std::uint64_t hashFields( const Field *fields, size_t count )
{
    std::uint64_t h = tileCheckpointHashInit();
    for ( size_t i = 0; i < count; ++i )
    {
        // Tag byte, then the value as 8 little-endian bytes — fixed order,
        // fixed width, append-only (the encoding IS the format).
        h = tileCheckpointHashStep( h, &fields[i].tag, 1 );
        unsigned char bytes[8];
        for ( int b = 0; b < 8; ++b )
            bytes[b] = static_cast<unsigned char>( ( fields[i].value >> ( 8 * b ) ) & 0xFF );
        h = tileCheckpointHashStep( h, bytes, sizeof( bytes ) );
    }
    return h;
}
} // namespace

std::uint64_t tileRunPartitionDigest( const TileRunPartition &p )
{
    const Field fields[] = {
        { 'W', static_cast<std::uint64_t>( static_cast<std::uint32_t>( p.rasterWidth ) ) },
        { 'H', static_cast<std::uint64_t>( static_cast<std::uint32_t>( p.rasterHeight ) ) },
        { 'w', static_cast<std::uint64_t>( static_cast<std::uint32_t>( p.tileWidth ) ) },
        { 'h', static_cast<std::uint64_t>( static_cast<std::uint32_t>( p.tileHeight ) ) },
        { 'k', static_cast<std::uint64_t>( static_cast<std::uint32_t>( p.halo ) ) },
        { 'b', static_cast<std::uint64_t>( static_cast<std::uint32_t>( p.bands ) ) },
        { 'o', static_cast<std::uint64_t>( static_cast<std::uint32_t>( p.bandOffset ) ) },
        { 't', static_cast<std::uint64_t>( static_cast<std::uint32_t>( p.timeIndex ) ) },
    };
    return hashFields( fields, sizeof( fields ) / sizeof( fields[0] ) );
}

std::string tileRunIdentityKey( const TileRunIdentity &identity )
{
    // '-' separator: the key doubles as a PATH COMPONENT (run directories,
    // state sidecars) and ':' is illegal in Win32 path components.
    char buf[3 * 16 + 2 + 1];
    std::snprintf( buf, sizeof( buf ), "%016llx-%016llx-%016llx",
                   static_cast<unsigned long long>( identity.operatorIdentity ),
                   static_cast<unsigned long long>( identity.inputIdentity ),
                   static_cast<unsigned long long>( identity.partitionDigest ) );
    return std::string( buf );
}

TileRunIdentity tileRunIdentityFromKey( const std::string &key )
{
    TileRunIdentity identity; // all-zero "nullopt shape" on malformed input
    // R35-T04 WP10 strict parse: sscanf accepted short groups ("1-2-3") and
    // trailing garbage ("...-...-...JUNK") — both silently produced a
    // well-formed identity the caller could not distinguish from a real one
    // (an all-zero identity IS legal, so the sentinel cannot encode failure).
    // The canonical form is EXACTLY 3×16 lowercase hex groups, '-'-joined,
    // length 3*16+2; anything else is malformed. Callers gate validity by
    // re-serializing: key(tileRunIdentityFromKey(k)) == k.
    if ( key.size() != 3 * 16 + 2 )
        return identity;
    const auto hexValue = []( char c ) -> int {
        if ( c >= '0' && c <= '9' )
            return c - '0';
        if ( c >= 'a' && c <= 'f' )
            return c - 'a' + 10;
        return -1;
    };
    std::uint64_t parts[3] = { 0, 0, 0 };
    for ( int group = 0; group < 3; ++group )
    {
        for ( int digit = 0; digit < 16; ++digit )
        {
            const int v = hexValue( key[static_cast<size_t>( group ) * 17
                                     + static_cast<size_t>( digit )] );
            if ( v < 0 )
                return identity;
            parts[group] = ( parts[group] << 4 ) | static_cast<std::uint64_t>( v );
        }
        // Separator between groups (positions 16 and 33) must be '-'.
        if ( group < 2 && key[static_cast<size_t>( group ) * 17 + 16] != '-' )
            return identity;
    }
    identity.operatorIdentity = parts[0];
    identity.inputIdentity = parts[1];
    identity.partitionDigest = parts[2];
    return identity;
}

} // namespace sicnu::runtime::chunk
