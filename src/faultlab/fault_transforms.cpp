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
#include <algorithm>

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


// --- ML/evaluation families (slice D) ----------------------------------------

FaultOutcome applyTrainTestSpatialLeakage( FaultGrid &grid, const Json::Value &params,
                                           std::uint32_t /*seed*/ )
{
    if ( !params.isMember( "mode" ) || !params["mode"].isString() )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "train_test_spatial_leakage needs a mode string" );
    }
    const std::string mode = params["mode"].asString();
    if ( mode != "duplicate" && mode != "relocate" )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "train_test_spatial_leakage mode '" + mode +
                                 "' is outside the closed vocabulary {duplicate, relocate}" );
    }
    if ( !grid.extras.isMember( "samples" ) || !grid.extras["samples"].isArray() )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "train_test_spatial_leakage needs a fixture with sample points" );
    }
    Json::Value &samples = grid.extras["samples"];

    if ( mode == "duplicate" )
    {
        // Clone every train sample into the test role: the classic
        // "same patch in train and test" leakage.
        Json::Value clones( Json::arrayValue );
        std::uint32_t cloned = 0;
        for ( const auto &sample : samples )
        {
            if ( sample.isObject() && sample.isMember( "role" ) &&
                 sample["role"].asString() == "train" )
            {
                Json::Value clone = sample;
                clone["role"] = "test";
                clones.append( clone );
                ++cloned;
            }
        }
        for ( const auto &clone : clones )
        {
            samples.append( clone );
        }
        if ( cloned == 0 )
        {
            return errorOutcome( "faultlab.fault_unsafe_target",
                                 "train_test_spatial_leakage found no train samples to clone" );
        }
        return okOutcome( cloned );
    }

    // relocate: move the first test sample onto the first train sample's
    // coordinates — a test point sitting inside a training region.
    int firstTrain = -1;
    int firstTest = -1;
    for ( Json::ArrayIndex i = 0; i < samples.size(); ++i )
    {
        const auto &sample = samples[i];
        if ( !sample.isObject() || !sample.isMember( "role" ) )
        {
            continue;
        }
        if ( firstTrain < 0 && sample["role"].asString() == "train" )
        {
            firstTrain = static_cast<int>( i );
        }
        if ( firstTest < 0 && sample["role"].asString() == "test" )
        {
            firstTest = static_cast<int>( i );
        }
    }
    if ( firstTrain < 0 || firstTest < 0 )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "train_test_spatial_leakage relocate needs train and test samples" );
    }
    samples[Json::ArrayIndex( firstTest )]["x"] = samples[Json::ArrayIndex( firstTrain )]["x"];
    samples[Json::ArrayIndex( firstTest )]["y"] = samples[Json::ArrayIndex( firstTrain )]["y"];
    return okOutcome( 1 );
}

FaultOutcome applyThresholdMisuse( FaultGrid &grid, const Json::Value &params,
                                    std::uint32_t /*seed*/ )
{
    if ( !grid.extras.isMember( "score" ) || !grid.extras["score"].isArray() )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "threshold_misuse needs a fixture with a score layer" );
    }
    if ( !params.isMember( "threshold" ) || !isFiniteNumber( params["threshold"] ) ||
         params["threshold"].asDouble() < 0.0 || params["threshold"].asDouble() > 1.0 )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "threshold_misuse needs a threshold in [0, 1]" );
    }
    const double threshold = params["threshold"].asDouble();
    if ( grid.extras.isMember( "threshold" ) && grid.extras["threshold"].isNumeric() &&
         grid.extras["threshold"].asDouble() == threshold )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "threshold_misuse requires a threshold different from the fixture's" );
    }
    grid.extras["threshold"] = threshold;
    return okOutcome( 1 );
}

FaultOutcome applyModelChannelMismatch( FaultGrid &grid, const Json::Value &params,
                                        std::uint32_t /*seed*/ )
{
    if ( !grid.extras.isMember( "model" ) || !grid.extras["model"].isObject() ||
         !grid.extras["model"].isMember( "channel_order" ) ||
         !grid.extras["model"]["channel_order"].isArray() )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "model_channel_mismatch needs a fixture with a declared model" );
    }
    const auto &order = grid.extras["model"]["channel_order"];
    if ( !params.isMember( "permutation" ) || !params["permutation"].isArray() )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "model_channel_mismatch needs a permutation array" );
    }
    const auto &permutation = params["permutation"];
    if ( permutation.size() != order.size() )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "model_channel_mismatch permutation must cover every channel" );
    }
    std::vector<bool> seen( static_cast<std::size_t>( order.size() ), false );
    std::vector<std::size_t> indices;
    bool identity = true;
    for ( Json::ArrayIndex i = 0; i < permutation.size(); ++i )
    {
        const auto &entry = permutation[i];
        if ( !entry.isIntegral() )
        {
            return errorOutcome( "faultlab.fault_unsupported_params",
                                 "model_channel_mismatch permutation entries must be integers" );
        }
        const std::int64_t raw = entry.asInt64();
        if ( raw < 0 || raw >= static_cast<std::int64_t>( order.size() ) )
        {
            return errorOutcome( "faultlab.fault_unsupported_params",
                                 "model_channel_mismatch permutation entry out of range" );
        }
        const auto index = static_cast<std::size_t>( raw );
        if ( seen[index] )
        {
            return errorOutcome( "faultlab.fault_unsupported_params",
                                 "model_channel_mismatch permutation is not a bijection" );
        }
        seen[index] = true;
        indices.push_back( index );
        if ( index != static_cast<std::size_t>( i ) )
        {
            identity = false;
        }
    }
    if ( identity )
    {
        return errorOutcome( "faultlab.fault_unsupported_params",
                             "model_channel_mismatch identity permutation is not a fault" );
    }

    Json::Value reordered( Json::arrayValue );
    for ( const auto index : indices )
    {
        reordered.append( order[Json::ArrayIndex( index )] );
    }
    grid.extras["model"]["channel_order"] = reordered;
    return okOutcome( 1 );
}


// --- artifact/provenance family (slice E) ------------------------------------

FaultOutcome applyProvenanceRemoval( FaultGrid &grid, const Json::Value &params,
                                     std::uint32_t /*seed*/ )
{
    std::string scope = "generator";
    if ( params.isMember( "scope" ) )
    {
        if ( !params["scope"].isString() )
        {
            return errorOutcome( "faultlab.fault_unsupported_params",
                                 "provenance_removal scope must be a string" );
        }
        scope = params["scope"].asString();
        if ( scope != "generator" && scope != "all" )
        {
            return errorOutcome( "faultlab.fault_unsupported_params",
                                 "provenance_removal scope '" + scope +
                                     "' is outside the closed vocabulary {generator, all}" );
        }
    }
    if ( !grid.extras.isMember( "provenance" ) || !grid.extras["provenance"].isObject() ||
         !grid.extras["provenance"].isMember( "generator" ) ||
         !grid.extras["provenance"]["generator"].isString() ||
         grid.extras["provenance"]["generator"].asString().empty() )
    {
        return errorOutcome( "faultlab.fault_unsafe_target",
                             "provenance_removal needs a fixture carrying generator provenance" );
    }

    if ( scope == "all" )
    {
        // Whole provenance block gone: the observable disappears rather
        // than reporting a faked default.
        grid.extras.removeMember( "provenance" );
        return okOutcome( 1 );
    }

    // Generator identity (and its seed) removed: the artifact can no longer
    // be traced back to the sample it was computed from.
    grid.extras["provenance"].removeMember( "generator" );
    grid.extras["provenance"].removeMember( "seed" );
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
    // ml/evaluation families (slice D)
    { "train_test_spatial_leakage", applyTrainTestSpatialLeakage },
    { "threshold_misuse", applyThresholdMisuse },
    { "model_channel_mismatch", applyModelChannelMismatch },
    // artifact/provenance family (slice E)
    { "provenance_removal", applyProvenanceRemoval },
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
