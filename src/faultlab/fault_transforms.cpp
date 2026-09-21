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

FaultOutcome applyBandRoleSwap( FaultGrid &grid, const Json::Value &params )
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

FaultOutcome applyOmitQualityMask( FaultGrid &grid, const Json::Value &params )
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

FaultOutcome applyWrongScaleOffset( FaultGrid &grid, const Json::Value &params )
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

FaultOutcome applyNoDataAsData( FaultGrid &grid, const Json::Value &params )
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

using TransformFn = FaultOutcome ( *)( FaultGrid &, const Json::Value & );

struct FamilyDispatch
{
    const char *familyId;
    TransformFn fn;
};

const FamilyDispatch kMetadataDispatches[] = {
    { "band_role_swap", applyBandRoleSwap },
    { "omit_quality_mask", applyOmitQualityMask },
    { "wrong_scale_offset", applyWrongScaleOffset },
    { "nodata_as_data", applyNoDataAsData },
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
    for ( const auto &dispatch : kMetadataDispatches )
    {
        if ( spec.familyId == dispatch.familyId )
        {
            return dispatch.fn( grid, spec.params );
        }
    }
    // Registered families outside the metadata slice have no transform yet;
    // they arrive here only before their slice lands.
    return errorOutcome( "faultlab.fault_unsupported",
                         "family '" + spec.familyId + "' has no transform in this build" );
}

} // namespace sicnu::faultlab
