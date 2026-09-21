// src/agent/autonomy/autonomy_level.cpp
#include "agent/autonomy/autonomy_level.h"

#include <array>

namespace sicnu::agent::autonomy {

std::string autonomyLevelToString( AutonomyLevel level )
{
    static const std::array<const char *, 6> kNames = { "L0", "L1", "L2", "L3", "L4", "L5" };
    const int ordinal = autonomyLevelOrdinal( level );
    return std::string( kNames[ static_cast<std::size_t>( ordinal ) ] );
}

bool autonomyLevelFromString( const std::string &text, AutonomyLevel &out )
{
    for ( int ordinal = 0; ordinal <= 5; ++ordinal )
    {
        if ( text == autonomyLevelToString( static_cast<AutonomyLevel>( ordinal ) ) )
        {
            out = static_cast<AutonomyLevel>( ordinal );
            return true;
        }
    }
    return false;
}

bool isKnownAutonomyLevelString( const std::string &text )
{
    AutonomyLevel out = AutonomyLevel::L0;
    return autonomyLevelFromString( text, out );
}

int autonomyLevelOrdinal( AutonomyLevel level )
{
    return static_cast<int>( level );
}

AutonomyLevel autonomyLevelFromOrdinal( int ordinal )
{
    if ( ordinal < 0 )
        return AutonomyLevel::L0;
    if ( ordinal > 5 )
        return AutonomyLevel::L5;
    return static_cast<AutonomyLevel>( ordinal );
}

} // namespace sicnu::agent::autonomy
