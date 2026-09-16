/***************************************************************************
 * rs_terrain_guard.h — shared fail-closed resource guards for the
 * full-frame terrain operators (track terrain-hydrology-11).
 ***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace sicnu::operators::rs {

/// Cell-count ceiling for full-frame terrain kernels. Full-frame kernels
/// allocate several float frames (see PERFORMANCE.md, terrain-hydrology-11);
/// above the ceiling they would die inside the allocator instead of failing
/// with a diagnosable error. Default 2²⁸ cells (16384² ≈ 6.4 GB per float
/// frame); override with SICNU_TERRAIN_MAX_CELLS (positive integer) for
/// hosts that knowingly have the memory.
inline std::uint64_t terrainMaxCells()
{
    const char *env = std::getenv( "SICNU_TERRAIN_MAX_CELLS" );
    if ( env && *env )
    {
        char *end = nullptr;
        const long long value = std::strtoll( env, &end, 10 );
        if ( end && *end == '\0' && value > 0 )
            return static_cast<std::uint64_t>( value );
    }
    return 268435456ULL;
}

/// True when @p width × @p height exceeds terrainMaxCells().
inline bool terrainExceedsCellBudget( int width, int height )
{
    if ( width <= 0 || height <= 0 )
        return false;
    const std::uint64_t cells = static_cast<std::uint64_t>( width )
                                * static_cast<std::uint64_t>( height );
    return cells > terrainMaxCells();
}

} // namespace sicnu::operators::rs
