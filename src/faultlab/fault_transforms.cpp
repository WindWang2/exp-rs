// fault_transforms.cpp — the fault transforms (see header for the contract).
//
// Shared param validation (closed per-family vocabulary, required presence)
// happens here before dispatch; each transform then validates its own
// semantics (targets exist, values are finite, permutations are bijections).
#include "fault_transforms.h"

#include "deterministic.h"
#include "fault_registry.h"

#include <json/json.h>

#include <cmath>
#include <string>
#include <vector>

namespace sicnu::faultlab
{

namespace
{

FaultOutcome okOutcome( std::uint32_t mutations )
{
    FaultOutcome outcome;
    outcome.ok = true;
    outcome.mutations = mutations;
    return outcome;
}

FaultOutcome errorOutcome( std::string code, std::string message )
{
    FaultOutcome outcome;
    outcome.ok = false;
    outcome.diagnostics.push_back(
        FaultDiagnostic{ std::move( code ), std::move( message ), FaultSeverity::Error } );
    return outcome;
}

bool isFiniteNumber( const Json::Value &value )
{
    return value.isNumeric() && std::isfinite( value.asDouble() );
}

/// Validates the closed param vocabulary plus required presence. Returns an
/// error outcome when a param name is unknown or a required param missing.
FaultOutcome validateParams( const FaultFamilyInfo &family, const Json::Value &params )
{
    if ( !params.isObject() )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "fault params must be a JSON object" );
    }
    for ( const auto &name : params.getMemberNames() )
    {
        if ( !family.hasParam( name ) )
        {
            return errorOutcome( "faultlab.fault_unsupported_params",
                                 "family '" + family.id + "' has no parameter '" + name + "'" );
        }
    }
    for ( const auto &name : family.requiredParams )
    {
        if ( !params.isMember( name ) )
        {
            return errorOutcome( "faultlab.fault_unsupported_params",
                                 "family '" + family.id + "' requires parameter '" + name + "'" );
        }
    }
    return okOutcome( 0 );
}

/// Counts finite (non-no-data) samples of a band.
std::uint32_t finiteCount( const BandSpec &band )
{
    std::uint32_t count = 0;
    for ( const double sample : band.samples )
    {
        if ( std::isfinite( sample ) )
        {
            ++count;
        }
    }
    return count;
}

// --- metadata families -------------------------------------------------------

FaultOutcome applyBandRoleSwap( FaultGrid &grid, const Json::Value &params,
                                 std::uint32_t /*seed*/ )
{
    const std::string roleA = params["role_a"].asString();
    const std::string roleB = params["role_b"].asString();
    if ( roleA == roleB )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "band_role_swap needs two distinct roles" );
    }
    const int indexA = grid.bandIndexByRole( roleA );
    const int indexB = grid.bandIndexByRole( roleB );
    if ( indexA < 0 || indexB < 0 )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "band_role_swap cannot find both roles in the fixture" );
    }
    std::swap( grid.bands[static_cast<std::size_t>( indexA )].role,
               grid.bands[static_cast<std::size_t>( indexB )].role );
    return okOutcome( 1 );
}

FaultOutcome applyOmitQualityMask( FaultGrid &grid, const Json::Value &params,
                                    std::uint32_t /*seed*/ )
{
    const std::string role = params.isMember( "role" ) ? params["role"].asString() : "qa";
    const int index = grid.bandIndexByRole( role );
    if ( index < 0 )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "omit_quality_mask cannot find role '" + role + "'" );
    }
    grid.bands.erase( grid.bands.begin() + index );
    return okOutcome( 1 );
}

FaultOutcome applyWrongScaleOffset( FaultGrid &grid, const Json::Value &params,
                                      std::uint32_t /*seed*/ )
{
    const std::string role = params["role"].asString();
    if ( !isFiniteNumber( params["gain"] ) || params["gain"].asDouble() == 0.0 )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "wrong_scale_offset needs a finite, nonzero gain" );
    }
    const double gain = params["gain"].asDouble();
    const double offset = params.isMember( "offset" ) && isFiniteNumber( params["offset"] )
                              ? params["offset"].asDouble()
                              : 0.0;
    const int index = grid.bandIndexByRole( role );
    if ( index < 0 )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "wrong_scale_offset cannot find role '" + role + "'" );
    }
    BandSpec &band = grid.bands[static_cast<std::size_t>( index )];

    if ( params.isMember( "metadata" ) && params["metadata"].isBool() &&
         params["metadata"].asBool() )
    {
        // Declared scale/offset wrong, pixel values kept: downstream products
        // silently misinterpret the data.
        band.scale *= gain;
        band.offset = band.offset * gain + offset;
        return okOutcome( 1 );
    }

    std::uint32_t mutations = 0;
    for ( double &sample : band.samples )
    {
        if ( std::isfinite( sample ) )
        {
            sample = sample * gain + offset;
            ++mutations;
        }
    }
    return okOutcome( mutations );
}

FaultOutcome applyNoDataAsData( FaultGrid &grid, const Json::Value &params,
                                  std::uint32_t /*seed*/ )
{
    double fill = grid.noDataValue;
    if ( params.isMember( "fill_value" ) )
    {
        if ( !isFiniteNumber( params["fill_value"] ) )
        {
            return errorOutcome( "faultlab.fault_unsupported_params",
                                 "nodata_as_data needs a finite fill_value" );
        }
        fill = params["fill_value"].asDouble();
    }
    if ( !std::isfinite( fill ) )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "nodata_as_data default fill (declared sentinel) is not finite" );
    }
    std::uint32_t mutations = 0;
    for ( auto &band : grid.bands )
    {
        for ( double &sample : band.samples )
        {
            if ( std::isnan( sample ) )
            {
                sample = fill;
                ++mutations;
            }
        }
    }
    return okOutcome( mutations );
}

// --- geometry/temporal families ---------------------------------------------

FaultOutcome applyGridShift( FaultGrid &grid, const Json::Value &params,
                              std::uint32_t /*seed*/ )
{
    // dx/dy are image-space pixel offsets (row direction y-down): the origin
    // moves by dx*pixelX / dy*pixelY, so a north-up grid (pixelY < 0) shifts
    // its origin north for a negative dy. Pixel size is untouched — the
    // classic sub-pixel misalignment that shows up as interpolation stripes.
    if ( !isFiniteNumber( params["dx"] ) || !isFiniteNumber( params["dy"] ) )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "grid_shift needs finite dx and dy (in pixels)" );
    }
    const double dx = params["dx"].asDouble();
    const double dy = params["dy"].asDouble();
    grid.geoTransform[0] += dx * grid.geoTransform[1];
    grid.geoTransform[3] += dy * grid.geoTransform[5];
    return okOutcome( 1 );
}

FaultOutcome applyCrsMismatch( FaultGrid &grid, const Json::Value &params,
                                 std::uint32_t /*seed*/ )
{
    if ( !params.isMember( "crs" ) || !params["crs"].isString() )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "crs_mismatch needs a crs string" );
    }
    const std::string crs = params["crs"].asString();
    // Closed vocabulary: the two CRS ids the teaching fixtures declare.
    if ( crs != "EPSG:4326" && crs != "EPSG:3857" )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "crs_mismatch crs '" + crs + "' is outside the closed vocabulary" );
    }
    if ( crs == grid.crsId )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "crs_mismatch requires a CRS different from the fixture's" );
    }
    grid.crsId = crs;
    return okOutcome( 1 );
}

FaultOutcome applyTemporalShuffle( FaultGrid &grid, const Json::Value &params,
                                   std::uint32_t seed )
{
    std::vector<std::string> dates;
    std::vector<std::size_t> datedBands;
    for ( std::size_t i = 0; i < grid.bands.size(); ++i )
    {
        if ( !grid.bands[i].acquisitionDate.empty() )
        {
            dates.push_back( grid.bands[i].acquisitionDate );
            datedBands.push_back( i );
        }
    }
    if ( dates.size() < 2 )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "temporal_shuffle needs at least two dated epochs" );
    }

    // Fisher-Yates over the date list with a purpose-derived seed, retrying
    // with derived purposes if the draw happens to be the identity, then a
    // bounded rotate-by-one fallback: the fault must always move the order.
    std::vector<std::string> shuffled = dates;
    std::uint32_t attempt = 0;
    while ( true )
    {
        std::string purpose = "temporal_shuffle";
        if ( attempt > 0 )
        {
            purpose += ".retry" + std::to_string( attempt );
        }
        deterministic::Pcg32 rng( deterministic::seedFor( seed, purpose ) );
        for ( int i = static_cast<int>( shuffled.size() ) - 1; i > 0; --i )
        {
            const int j = static_cast<int>( rng.nextBounded( static_cast<std::uint32_t>( i + 1 ) ) );
            std::swap( shuffled[static_cast<std::size_t>( i )],
                       shuffled[static_cast<std::size_t>( j )] );
        }
        if ( shuffled != dates )
        {
            break;
        }
        ++attempt;
        if ( attempt > 8 )
        {
            std::rotate( shuffled.begin(), shuffled.begin() + 1, shuffled.end() );
            break;
        }
    }

    for ( std::size_t k = 0; k < datedBands.size(); ++k )
    {
        grid.bands[datedBands[k]].acquisitionDate = shuffled[k];
    }
    return okOutcome( 1 );
}

FaultOutcome applyTemporalGap( FaultGrid &grid, const Json::Value &params,
                                std::uint32_t /*seed*/ )
{
    if ( !params.isMember( "epoch_index" ) || !params["epoch_index"].isIntegral() )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "temporal_gap needs an integer epoch_index" );
    }
    const auto raw = params["epoch_index"].asInt64();
    if ( raw < 0 || raw >= static_cast<std::int64_t>( grid.bands.size() ) )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "temporal_gap epoch_index is outside the fixture's band range" );
    }
    const auto index = static_cast<std::size_t>( raw );
    if ( grid.bands[index].acquisitionDate.empty() )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "temporal_gap target band carries no acquisition date" );
    }
    grid.bands.erase( grid.bands.begin() + static_cast<std::ptrdiff_t>( index ) );
    return okOutcome( 1 );
}

using TransformFn = FaultOutcome ( *)( FaultGrid &, const Json::Value &, std::uint32_t );

struct FamilyDispatch
{
    const char *familyId;
    TransformFn fn;
};

const FamilyDispatch kDispatches[] = {
    // metadata families (slice B)
    { "band_role_swap", applyBandRoleSwap },
    { "omit_quality_mask", applyOmitQualityMask },
    { "wrong_scale_offset", applyWrongScaleOffset },
    { "nodata_as_data", applyNoDataAsData },
    // geometry/temporal families (slice C)
    { "grid_shift", applyGridShift },
    { "crs_mismatch", applyCrsMismatch },
    { "temporal_gap", applyTemporalGap },
    { "temporal_shuffle", applyTemporalShuffle },
};

} // namespace

FaultOutcome applyFault( FaultGrid &grid, const FaultSpec &spec )
{
    const FaultFamilyInfo *family = findFaultFamily( spec.familyId );
    if ( family == nullptr )
    {
        return errorOutcome( "faultlab.fault_unknown_family",
                             "unknown fault family '" + spec.familyId + "'" );
    }
    const FaultOutcome validated = validateParams( *family, spec.params );
    if ( !validated.ok )
    {
        return validated;
    }
    for ( const auto &dispatch : kDispatches )
    {
        if ( spec.familyId == dispatch.familyId )
        {
            return dispatch.fn( grid, spec.params, spec.seed );
        }
    }
    // Registered families outside the metadata slice have no transform yet;
    // they arrive here only before their slice lands.
    return errorOutcome( "faultlab.fault_unsupported",
                         "family '" + spec.familyId + "' has no transform in this build" );
}

} // namespace sicnu::faultlab
