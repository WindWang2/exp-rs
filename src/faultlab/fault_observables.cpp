// fault_observables.cpp — observable measurement (see header for the id
// conventions and the "never fake a missing observable" rule).
#include "fault_observables.h"

#include "util/canonical_json.h"

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::faultlab
{

namespace
{

Observable numberObservable( const std::string &id, double value )
{
    Observable observable;
    observable.id = id;
    observable.kind = ObservableKind::Number;
    observable.number = value;
    return observable;
}

Observable textObservable( const std::string &id, const std::string &text )
{
    Observable observable;
    observable.id = id;
    observable.kind = ObservableKind::Text;
    observable.text = text;
    return observable;
}

Observable truthObservable( const std::string &id, bool truth )
{
    Observable observable;
    observable.id = id;
    observable.kind = ObservableKind::Truth;
    observable.truth = truth;
    return observable;
}

std::string join( const std::vector<std::string> &parts, const char *separator = "," )
{
    std::string out;
    for ( std::size_t i = 0; i < parts.size(); ++i )
    {
        if ( i != 0 )
        {
            out += separator;
        }
        out += parts[i];
    }
    return out;
}

bool qaInvalidAt( const FaultGrid &grid, int qaBand, std::size_t index )
{
    if ( qaBand < 0 )
    {
        return false;
    }
    const auto &qa = grid.bands[static_cast<std::size_t>( qaBand )];
    if ( index >= qa.samples.size() )
    {
        return false;
    }
    const double value = qa.samples[index];
    return std::isfinite( value ) && value == 0.0;
}

/// Counts finite (non-no-data) samples of a band.
std::uint64_t finiteCount( const BandSpec &band )
{
    std::uint64_t count = 0;
    for ( const double sample : band.samples )
    {
        if ( std::isfinite( sample ) )
        {
            ++count;
        }
    }
    return count;
}

/// Cohen's kappa of a binary prediction against binary truth; the standard
/// (po - pe) / (1 - pe) form. Degenerate (constant) cases return -1, the
/// "not computable" marker the lab diagnostics already understands.
double cohenKappa( const std::vector<double> &prediction, const std::vector<double> &truth )
{
    if ( prediction.size() != truth.size() || prediction.empty() )
    {
        return -1.0;
    }
    double truePositive = 0, trueNegative = 0, falsePositive = 0, falseNegative = 0;
    for ( std::size_t i = 0; i < prediction.size(); ++i )
    {
        const bool p = prediction[i] != 0.0;
        const bool t = truth[i] != 0.0;
        if ( p && t )
        {
            ++truePositive;
        }
        else if ( !p && !t )
        {
            ++trueNegative;
        }
        else if ( p && !t )
        {
            ++falsePositive;
        }
        else
        {
            ++falseNegative;
        }
    }
    const double observed = ( truePositive + trueNegative ) / double( prediction.size() );
    const double predYes = ( truePositive + falsePositive ) / double( prediction.size() );
    const double truthYes = ( truePositive + falseNegative ) / double( prediction.size() );
    const double expected = predYes * truthYes + ( 1.0 - predYes ) * ( 1.0 - truthYes );
    if ( expected >= 1.0 )
    {
        return -1.0;
    }
    return ( observed - expected ) / ( 1.0 - expected );
}

void measureExtras( const FaultGrid &grid, ObservableSet &out )
{
    const Json::Value &extras = grid.extras;

    // Role-resolved ratio index (e.g. NDVI) when the fixture declares the pair.
    if ( extras.isMember( "index_pair" ) && extras["index_pair"].isObject() )
    {
        const auto &pair = extras["index_pair"];
        if ( pair.isMember( "numerator" ) && pair.isMember( "denominator" ) )
        {
            const int numerator = grid.bandIndexByRole( pair["numerator"].asString() );
            const int denominator = grid.bandIndexByRole( pair["denominator"].asString() );
            if ( numerator >= 0 && denominator >= 0 )
            {
                const auto &num = grid.bands[static_cast<std::size_t>( numerator )];
                const auto &den = grid.bands[static_cast<std::size_t>( denominator )];
                double sum = 0.0;
                std::uint32_t count = 0;
                const std::size_t samples = std::min( num.samples.size(), den.samples.size() );
                for ( std::size_t i = 0; i < samples; ++i )
                {
                    const double n = num.samples[i];
                    const double d = den.samples[i];
                    if ( std::isfinite( n ) && std::isfinite( d ) && ( n + d ) != 0.0 )
                    {
                        sum += ( n - d ) / ( n + d );
                        ++count;
                    }
                }
                if ( count > 0 )
                {
                    const std::string id = pair.isMember( "id" ) && pair["id"].isString()
                                               ? pair["id"].asString()
                                               : "index_mean";
                    out.insert( { id, numberObservable( id, sum / double( count ) ) } );
                }
            }
        }
    }

    // Train/test sample points for the leakage family.
    if ( extras.isMember( "samples" ) && extras["samples"].isArray() )
    {
        double cellSize = 4.0;
        if ( extras.isMember( "leakage_cell_size" ) && extras["leakage_cell_size"].isNumeric() )
        {
            cellSize = extras["leakage_cell_size"].asDouble();
        }
        std::vector<std::pair<double, double>> trainCells;
        std::vector<std::pair<double, double>> testCells;
        for ( const auto &sample : extras["samples"] )
        {
            if ( !sample.isObject() || !sample.isMember( "role" ) || !sample.isMember( "x" ) ||
                 !sample.isMember( "y" ) )
            {
                continue;
            }
            const auto cell = std::make_pair( std::floor( sample["x"].asDouble() / cellSize ),
                                              std::floor( sample["y"].asDouble() / cellSize ) );
            if ( sample["role"].asString() == "train" )
            {
                trainCells.push_back( cell );
            }
            else if ( sample["role"].asString() == "test" )
            {
                testCells.push_back( cell );
            }
        }
        std::uint32_t overlap = 0;
        for ( const auto &testCell : testCells )
        {
            for ( const auto &trainCell : trainCells )
            {
                if ( testCell == trainCell )
                {
                    ++overlap;
                    break;
                }
            }
        }
        out.insert( { "leakage.test_count",
                      numberObservable( "leakage.test_count", double( testCells.size() ) ) } );
        out.insert( { "leakage.overlap_fraction",
                      numberObservable( "leakage.overlap_fraction",
                                        testCells.empty()
                                            ? 0.0
                                            : double( overlap ) / double( testCells.size() ) ) } );
    }

    // Score layer + truth labels for the threshold family.
    if ( extras.isMember( "score" ) && extras["score"].isArray() )
    {
        std::vector<double> score;
        for ( const auto &value : extras["score"] )
        {
            if ( value.isNumeric() )
            {
                score.push_back( value.asDouble() );
            }
        }
        double threshold = 0.5;
        if ( extras.isMember( "threshold" ) && extras["threshold"].isNumeric() )
        {
            threshold = extras["threshold"].asDouble();
        }
        std::uint32_t positive = 0;
        for ( const double value : score )
        {
            if ( value > threshold )
            {
                ++positive;
            }
        }
        out.insert( { "threshold", numberObservable( "threshold", threshold ) } );
        out.insert( { "positive_fraction",
                      numberObservable( "positive_fraction",
                                        score.empty() ? 0.0 : double( positive ) / double( score.size() ) ) } );

        if ( extras.isMember( "truth" ) && extras["truth"].isArray() )
        {
            std::vector<double> truth;
            std::vector<double> prediction;
            const auto &truthJson = extras["truth"];
            const std::size_t samples = std::min( score.size(), std::size_t( truthJson.size() ) );
            for ( std::size_t i = 0; i < samples; ++i )
            {
                if ( !truthJson[Json::ArrayIndex( i )].isNumeric() )
                {
                    continue;
                }
                truth.push_back( truthJson[Json::ArrayIndex( i )].asDouble() );
                prediction.push_back( score[i] > threshold ? 1.0 : 0.0 );
            }
            out.insert( { "kappa", numberObservable( "kappa", cohenKappa( prediction, truth ) ) } );
        }
    }

    // Linear model over a declared channel order for the channel family.
    if ( extras.isMember( "model" ) && extras["model"].isObject() )
    {
        const auto &model = extras["model"];
        if ( model.isMember( "channel_order" ) && model["channel_order"].isArray() &&
             model.isMember( "weights" ) && model["weights"].isArray() )
        {
            std::vector<std::string> order;
            std::vector<double> weights;
            for ( const auto &role : model["channel_order"] )
            {
                if ( role.isString() )
                {
                    order.push_back( role.asString() );
                }
            }
            for ( const auto &weight : model["weights"] )
            {
                if ( weight.isNumeric() )
                {
                    weights.push_back( weight.asDouble() );
                }
            }
            if ( order.size() == weights.size() && !order.empty() )
            {
                std::vector<int> indices;
                bool resolvable = true;
                for ( const auto &role : order )
                {
                    const int index = grid.bandIndexByRole( role );
                    if ( index < 0 )
                    {
                        resolvable = false;
                        break;
                    }
                    indices.push_back( index );
                }
                if ( resolvable )
                {
                    const double bias =
                        model.isMember( "bias" ) && model["bias"].isNumeric()
                            ? model["bias"].asDouble()
                            : 0.0;
                    double sum = 0.0;
                    std::uint32_t count = 0;
                    const std::size_t samples = grid.sampleCount();
                    for ( std::size_t i = 0; i < samples; ++i )
                    {
                        double value = bias;
                        bool complete = true;
                        for ( std::size_t k = 0; k < indices.size(); ++k )
                        {
                            const auto &band = grid.bands[static_cast<std::size_t>( indices[k] )];
                            if ( i >= band.samples.size() || !std::isfinite( band.samples[i] ) )
                            {
                                complete = false;
                                break;
                            }
                            value += weights[k] * band.samples[i];
                        }
                        if ( complete )
                        {
                            sum += value;
                            ++count;
                        }
                    }
                    const std::string orderText = join( order );
                    out.insert( { "channel_order", textObservable( "channel_order", orderText ) } );
                    if ( count > 0 )
                    {
                        out.insert( { "model_output_mean",
                                      numberObservable( "model_output_mean", sum / double( count ) ) } );
                    }
                }
            }
        }
    }

    // Provenance block for the artifact family.
    if ( extras.isMember( "provenance" ) && extras["provenance"].isObject() )
    {
        const auto &provenance = extras["provenance"];
        const bool present = provenance.isMember( "generator" ) &&
                             provenance["generator"].isString() &&
                             !provenance["generator"].asString().empty();
        out.insert( { "provenance.generator_present",
                      truthObservable( "provenance.generator_present", present ) } );
    }
}

} // namespace

ObservableSet measureObservables( const FaultGrid &grid )
{
    ObservableSet out;

    out.insert( { "band_count", numberObservable( "band_count", double( grid.bands.size() ) ) } );

    std::vector<std::string> roles;
    roles.reserve( grid.bands.size() );
    for ( const auto &band : grid.bands )
    {
        roles.push_back( band.role );
    }
    out.insert( { "band_roles", textObservable( "band_roles", join( roles ) ) } );
    out.insert( { "crs", textObservable( "crs", grid.crsId ) } );
    out.insert( { "geo_transform.origin_x",
                  numberObservable( "geo_transform.origin_x", grid.geoTransform[0] ) } );
    out.insert( { "geo_transform.origin_y",
                  numberObservable( "geo_transform.origin_y", grid.geoTransform[3] ) } );

    const int qaBand = grid.bandIndexByRole( "qa" );
    std::uint64_t samples = 0;
    std::uint64_t valid = 0;
    std::uint64_t finite = 0;
    for ( std::size_t b = 0; b < grid.bands.size(); ++b )
    {
        if ( static_cast<int>( b ) == qaBand )
        {
            continue; // the mask itself is not masked data
        }
        const auto &band = grid.bands[b];
        for ( std::size_t i = 0; i < band.samples.size(); ++i )
        {
            ++samples;
            if ( std::isfinite( band.samples[i] ) )
            {
                ++finite;
                if ( !qaInvalidAt( grid, qaBand, i ) )
                {
                    ++valid;
                }
            }
        }
    }
    const double total = samples == 0 ? 0.0 : double( samples );
    out.insert( { "nodata_fraction", numberObservable( "nodata_fraction",
                                                       1.0 - finite / ( total == 0.0 ? 1.0 : total ) ) } );
    out.insert( { "finite_fraction", numberObservable( "finite_fraction",
                                                       finite / ( total == 0.0 ? 1.0 : total ) ) } );
    out.insert( { "valid_fraction", numberObservable( "valid_fraction",
                                                      valid / ( total == 0.0 ? 1.0 : total ) ) } );

    for ( const auto &band : grid.bands )
    {
        const std::string suffix = "." + band.role;
        double sum = 0.0;
        double min = 0.0;
        double max = 0.0;
        bool first = true;
        for ( const double sample : band.samples )
        {
            if ( !std::isfinite( sample ) )
            {
                continue;
            }
            sum += sample;
            if ( first )
            {
                min = sample;
                max = sample;
                first = false;
            }
            else
            {
                min = std::min( min, sample );
                max = std::max( max, sample );
            }
        }
        if ( !first )
        {
            const std::uint64_t count = finiteCount( band );
            out.insert( { "band.mean" + suffix,
                          numberObservable( "band.mean" + suffix, sum / double( count ) ) } );
            out.insert( { "band.min" + suffix,
                          numberObservable( "band.min" + suffix, min ) } );
            out.insert( { "band.max" + suffix,
                          numberObservable( "band.max" + suffix, max ) } );
        }
        out.insert( { "band.scale" + suffix,
                      numberObservable( "band.scale" + suffix, band.scale ) } );
        out.insert( { "band.offset" + suffix,
                      numberObservable( "band.offset" + suffix, band.offset ) } );
    }

    std::vector<std::string> dates;
    for ( const auto &band : grid.bands )
    {
        if ( !band.acquisitionDate.empty() )
        {
            dates.push_back( band.acquisitionDate );
        }
    }
    if ( !dates.empty() )
    {
        out.insert( { "acquisition_dates", textObservable( "acquisition_dates", join( dates ) ) } );
    }

    measureExtras( grid, out );
    return out;
}

const Observable *findObservable( const ObservableSet &set, const std::string &id )
{
    const auto it = set.find( id );
    return it == set.end() ? nullptr : &it->second;
}

} // namespace sicnu::faultlab
