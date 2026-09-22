// fault_fixtures.cpp — the deterministic base-fixture factory.
#include "fault_fixtures.h"

#include "deterministic.h"

#include <json/json.h>

#include <cmath>

#include <string>
#include <vector>

namespace sicnu::faultlab
{

namespace
{

constexpr int kFixtureWidth = 16;
constexpr int kFixtureHeight = 16;
constexpr std::size_t kFixtureSamples = kFixtureWidth * kFixtureHeight;

/// Symmetric noise in [-amplitude, +amplitude) from the fixture's own stream.
double noise( deterministic::Pcg32 &rng, double amplitude )
{
    return ( rng.nextDouble() - 0.5 ) * 2.0 * amplitude;
}

/// Red/NIR closed form with a strong, sign-robust NDVI signal (min |index|
/// ≈ 0.37) so role swaps flip the sign deterministically.
void fillIndexPair( FaultGrid &grid, deterministic::Pcg32 &rng )
{
    grid.width = kFixtureWidth;
    grid.height = kFixtureHeight;
    grid.crsId = "EPSG:4326";
    grid.hasNoData = false;

    BandSpec red;
    red.role = "red";
    BandSpec nir;
    nir.role = "nir";
    red.samples.reserve( kFixtureSamples );
    nir.samples.reserve( kFixtureSamples );
    for ( int i = 0; i < kFixtureHeight; ++i )
    {
        for ( int j = 0; j < kFixtureWidth; ++j )
        {
            const double t = double( i * kFixtureWidth + j ) / double( kFixtureSamples - 1 );
            red.samples.push_back( 0.10 + 0.15 * t + noise( rng, 0.02 ) );
            nir.samples.push_back( 0.55 + 0.30 * t + noise( rng, 0.02 ) );
        }
    }
    grid.bands = { red, nir };

    Json::Value pair( Json::objectValue );
    pair["numerator"] = "nir";
    pair["denominator"] = "red";
    pair["id"] = "index_mean";
    grid.extras["index_pair"] = pair;
}

FaultGrid twoBandIndexPair( std::uint32_t seed )
{
    deterministic::Pcg32 rng( deterministic::seedFor( seed, "two_band_index_pair" ) );
    FaultGrid grid;
    fillIndexPair( grid, rng );
    return grid;
}

FaultGrid qualityMaskedPair( std::uint32_t seed )
{
    deterministic::Pcg32 rng( deterministic::seedFor( seed, "quality_masked_pair" ) );
    FaultGrid grid;
    fillIndexPair( grid, rng );

    BandSpec qa;
    qa.role = "qa";
    qa.samples.reserve( kFixtureSamples );
    std::size_t index = 0;
    for ( int i = 0; i < kFixtureHeight; ++i )
    {
        for ( int j = 0; j < kFixtureWidth; ++j )
        {
            // Cloud stripes every 7th pixel; half of them are additionally
            // NaN in the data bands so dropping the mask moves valid_fraction.
            const bool cloud = ( index % 7 ) == 0;
            const bool nanToo = ( index % 14 ) == 0;
            qa.samples.push_back( cloud ? 0.0 : 1.0 );
            if ( nanToo )
            {
                grid.bands[0].samples[index] = std::nan( "" );
                grid.bands[1].samples[index] = std::nan( "" );
            }
            ++index;
        }
    }
    grid.hasNoData = true;
    grid.noDataValue = -9999.0;
    grid.bands.push_back( qa );
    return grid;
}

FaultGrid temporalStack( std::uint32_t seed )
{
    deterministic::Pcg32 rng( deterministic::seedFor( seed, "temporal_stack" ) );
    FaultGrid grid;
    grid.width = kFixtureWidth;
    grid.height = kFixtureHeight;
    grid.crsId = "EPSG:4326";
    const char *dates[4] = { "2020-03-01", "2020-04-01", "2020-05-01", "2020-06-01" };
    const char *roles[4] = { "epoch1", "epoch2", "epoch3", "epoch4" };
    const double levels[4] = { 0.20, 0.40, 0.60, 0.80 };
    for ( int band = 0; band < 4; ++band )
    {
        BandSpec spec;
        spec.role = roles[band];
        spec.acquisitionDate = dates[band];
        spec.samples.reserve( kFixtureSamples );
        for ( std::size_t index = 0; index < kFixtureSamples; ++index )
        {
            const double t = double( index ) / double( kFixtureSamples - 1 );
            spec.samples.push_back( levels[band] + 0.05 * t + noise( rng, 0.01 ) );
        }
        grid.bands.push_back( spec );
    }
    return grid;
}

FaultGrid classificationGrid( std::uint32_t seed )
{
    deterministic::Pcg32 rng( deterministic::seedFor( seed, "classification_grid" ) );
    FaultGrid grid;
    grid.width = kFixtureWidth;
    grid.height = kFixtureHeight;
    grid.crsId = "EPSG:4326";

    // Label raster: three closed-form zones (top-left 0, top-right 1,
    // bottom 2).
    BandSpec labels;
    labels.role = "labels";
    labels.samples.reserve( kFixtureSamples );
    Json::Value labelJson( Json::arrayValue );
    for ( int i = 0; i < kFixtureHeight; ++i )
    {
        for ( int j = 0; j < kFixtureWidth; ++j )
        {
            double label = ( i < 8 ) ? ( ( j < 8 ) ? 0.0 : 1.0 ) : 2.0;
            labels.samples.push_back( label );
            labelJson.append( Json::Value::Int64( static_cast<Json::Int64>( label ) ) );
        }
    }
    grid.bands = { labels };
    grid.extras["labels"] = labelJson;

    // Train points in the top-left zone, test points in the bottom-right
    // zone: disjoint 4x4 cells, leakage.overlap_fraction = 0 when clean.
    Json::Value samples( Json::arrayValue );
    // Seed jitter (±0.3) keeps points inside their disjoint 4x4 cells while
    // making every seed produce a distinct fixture.
    auto addSample = [&samples, &rng]( const char *role, double x, double y ) {
        Json::Value sample( Json::objectValue );
        sample["role"] = role;
        sample["x"] = x + noise( rng, 0.3 );
        sample["y"] = y + noise( rng, 0.3 );
        samples.append( sample );
    };
    addSample( "train", 1.0, 1.0 );
    addSample( "train", 2.0, 2.0 );
    addSample( "train", 3.0, 3.0 );
    addSample( "train", 6.0, 6.0 );
    addSample( "test", 9.0, 9.0 );
    addSample( "test", 10.0, 10.0 );
    addSample( "test", 11.0, 11.0 );
    addSample( "test", 14.0, 14.0 );
    grid.extras["samples"] = samples;
    grid.extras["leakage_cell_size"] = 4.0;
    return grid;
}

FaultGrid probabilityLayer( std::uint32_t seed )
{
    deterministic::Pcg32 rng( deterministic::seedFor( seed, "probability_layer" ) );
    FaultGrid grid;
    grid.width = kFixtureWidth;
    grid.height = kFixtureHeight;
    grid.crsId = "EPSG:4326";

    // Scores split exactly at 0.5: the top half of rows are positives with
    // scores in [0.55, 0.94], the bottom half negatives in [0.06, 0.45].
    // A destructive 0.95 cut predicts all-negative, so kappa collapses to 0.
    Json::Value score( Json::arrayValue );
    Json::Value truth( Json::arrayValue );
    for ( int i = 0; i < kFixtureHeight; ++i )
    {
        for ( int j = 0; j < kFixtureWidth; ++j )
        {
            const double u = double( ( i * kFixtureWidth + j ) % kFixtureWidth ) /
                             double( kFixtureWidth - 1 );
            const bool positive = i < kFixtureHeight / 2;
            const double value = positive ? ( 0.55 + 0.39 * u + noise( rng, 0.005 ) )
                                          : ( 0.45 - 0.39 * u + noise( rng, 0.005 ) );
            score.append( value );
            truth.append( positive ? 1 : 0 );
        }
    }
    grid.extras["score"] = score;
    grid.extras["truth"] = truth;
    grid.extras["threshold"] = 0.5;
    return grid;
}

FaultGrid modelInputStack( std::uint32_t seed )
{
    deterministic::Pcg32 rng( deterministic::seedFor( seed, "model_input_stack" ) );
    FaultGrid grid;
    grid.width = kFixtureWidth;
    grid.height = kFixtureHeight;
    grid.crsId = "EPSG:4326";

    const double levels[3] = { 1.0, 2.0, 3.0 };
    const char *roles[3] = { "red", "nir", "green" };
    for ( int band = 0; band < 3; ++band )
    {
        BandSpec spec;
        spec.role = roles[band];
        spec.samples.reserve( kFixtureSamples );
        for ( std::size_t index = 0; index < kFixtureSamples; ++index )
        {
            spec.samples.push_back( levels[band] + noise( rng, 0.01 ) );
        }
        grid.bands.push_back( spec );
    }

    Json::Value model( Json::objectValue );
    Json::Value order( Json::arrayValue );
    order.append( "red" );
    order.append( "nir" );
    order.append( "green" );
    model["channel_order"] = order;
    Json::Value weights( Json::arrayValue );
    weights.append( 1.0 );
    weights.append( 2.0 );
    weights.append( 3.0 );
    model["weights"] = weights;
    model["bias"] = 0.1;
    grid.extras["model"] = model;
    return grid;
}

FaultGrid provenancedPair( std::uint32_t seed )
{
    deterministic::Pcg32 rng( deterministic::seedFor( seed, "provenanced_pair" ) );
    FaultGrid grid;
    fillIndexPair( grid, rng );

    Json::Value provenance( Json::objectValue );
    provenance["schema"] = "exp-rs-prov/1";
    provenance["generator"] = "faultlab.fixture_factory";
    provenance["seed"] = seed;
    provenance["product"] = "provenanced_pair";
    grid.extras["provenance"] = provenance;
    return grid;
}

} // namespace

const std::vector<std::string> &fixtureIds()
{
    static const std::vector<std::string> ids = {
        "two_band_index_pair", "quality_masked_pair", "temporal_stack",
        "classification_grid", "probability_layer", "model_input_stack",
        "provenanced_pair",
    };
    return ids;
}

FaultResult<FaultGrid> makeFixture( const std::string &id, const Json::Value &params,
                                    std::uint32_t seed )
{
    (void)params; // v1 fixtures take no parameters; the field exists for schema symmetry.
    if ( id == "two_band_index_pair" )
    {
        return makeOk( twoBandIndexPair( seed ) );
    }
    if ( id == "quality_masked_pair" )
    {
        return makeOk( qualityMaskedPair( seed ) );
    }
    if ( id == "temporal_stack" )
    {
        return makeOk( temporalStack( seed ) );
    }
    if ( id == "classification_grid" )
    {
        return makeOk( classificationGrid( seed ) );
    }
    if ( id == "probability_layer" )
    {
        return makeOk( probabilityLayer( seed ) );
    }
    if ( id == "model_input_stack" )
    {
        return makeOk( modelInputStack( seed ) );
    }
    if ( id == "provenanced_pair" )
    {
        return makeOk( provenancedPair( seed ) );
    }
    return makeError<FaultGrid>( "faultlab.fixture_unknown", "unknown base fixture '" + id + "'" );
}

} // namespace sicnu::faultlab
