// mosaic_quality.cpp — F15 Package E implementation (ADR 0163).
#include "mosaic_quality.h"

#include <algorithm>
#include <cmath>

namespace rs::mosaic {

QualityScore QualityScorer::score( const SceneQualityInput &input, const QualityWeights &weights,
                                   const QualityOptions &options )
{
    QualityScore result;

    // Dimension scores, each 0..1 (1 = best).
    double sCloud = 1.0, sQuality = 1.0, sTime = 1.0, sView = 1.0;
    double wSum = 0.0;

    if ( input.hasCloud )
    {
        const double f = std::clamp( input.cloudFraction, 0.0, 1.0 );
        result.clamped |= ( f != input.cloudFraction );
        sCloud = 1.0 - f;
        wSum += std::max( 0.0, weights.cloud );
    }
    if ( input.hasQuality )
    {
        const double q = std::clamp( input.quality, 0.0, 1.0 );
        result.clamped |= ( q != input.quality );
        sQuality = q;
        wSum += std::max( 0.0, weights.quality );
    }
    if ( input.hasTime )
    {
        const double t = std::abs( input.timeDays );
        if ( t < 0.0 )
            result.clamped = true;
        const double scaled = t / std::max( 1e-9, options.timeScaleDays );
        sTime = std::clamp( 1.0 - scaled, 0.0, 1.0 );
        result.clamped |= ( t > options.timeScaleDays );
        wSum += std::max( 0.0, weights.time );
    }
    if ( input.hasView )
    {
        const double a = std::abs( input.viewAngleDeg );
        const double scaled = a / std::max( 1e-9, options.viewScaleDeg );
        sView = std::clamp( 1.0 - scaled, 0.0, 1.0 );
        result.clamped |= ( a > options.viewScaleDeg );
        wSum += std::max( 0.0, weights.view );
    }

    if ( wSum <= 0.0 )
    {
        // No quality dimension provided: neutral score, ordered by
        // priority/index downstream.
        result.score = 1.0;
        return result;
    }

    double acc = 0.0;
    if ( input.hasCloud )
        acc += std::max( 0.0, weights.cloud ) * sCloud;
    if ( input.hasQuality )
        acc += std::max( 0.0, weights.quality ) * sQuality;
    if ( input.hasTime )
        acc += std::max( 0.0, weights.time ) * sTime;
    if ( input.hasView )
        acc += std::max( 0.0, weights.view ) * sView;

    result.score = std::clamp( acc / wSum, 0.0, 1.0 );
    return result;
}

std::vector<int> QualityScorer::compositeOrder( const std::vector<QualityScore> &scores,
                                                const std::vector<int> &priorities )
{
    std::vector<int> order( scores.size() );
    for ( size_t i = 0; i < order.size(); ++i )
        order[i] = static_cast<int>( i );
    std::stable_sort( order.begin(), order.end(), [&]( int a, int b ) {
        if ( scores[a].score != scores[b].score )
            return scores[a].score > scores[b].score; // best first
        const int pa = a < static_cast<int>( priorities.size() ) ? priorities[a] : 0;
        const int pb = b < static_cast<int>( priorities.size() ) ? priorities[b] : 0;
        if ( pa != pb )
            return pa < pb;
        return a < b;
    } );
    return order;
}

} // namespace rs::mosaic
