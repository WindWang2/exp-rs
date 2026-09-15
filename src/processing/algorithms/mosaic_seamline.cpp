// mosaic_seamline.cpp — F15 Package C implementation (ADR 0163).
#include "mosaic_seamline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rs::mosaic {
namespace {

constexpr float kInvalidCellCost = 1e6f;

std::vector<int> seamVertical( const std::vector<float> &cost, int width, int height,
                               double *totalCost )
{
    // acc[r][c]: min accumulated cost to reach (r, c). parent[r][c]: chosen
    // previous column. Ties break to the smaller previous column because
    // candidates are scanned in increasing column order with strict <.
    std::vector<double> acc( static_cast<size_t>( width ) * height, 0.0 );
    std::vector<int32_t> parent( static_cast<size_t>( width ) * height, 0 );
    for ( int c = 0; c < width; ++c )
        acc[c] = cost[c];

    for ( int r = 1; r < height; ++r )
    {
        const size_t rowBase = static_cast<size_t>( r ) * width;
        const size_t prevBase = rowBase - width;
        for ( int c = 0; c < width; ++c )
        {
            int bestPc = std::max( 0, c - 1 );
            double best = acc[prevBase + bestPc];
            for ( int pc = std::max( 0, c - 1 ) + 1; pc <= std::min( width - 1, c + 1 ); ++pc )
            {
                const double v = acc[prevBase + pc];
                if ( v < best )
                {
                    best = v;
                    bestPc = pc;
                }
            }
            acc[rowBase + c] = best + cost[rowBase + c];
            parent[rowBase + c] = bestPc;
        }
    }

    // Best final column; ties break to the smaller column (strict <).
    int col = 0;
    double best = acc[static_cast<size_t>( height - 1 ) * width];
    for ( int c = 1; c < width; ++c )
    {
        const double v = acc[static_cast<size_t>( height - 1 ) * width + c];
        if ( v < best )
        {
            best = v;
            col = c;
        }
    }

    std::vector<int> path( height );
    int r = height - 1;
    while ( r >= 0 )
    {
        path[r] = col;
        if ( r > 0 )
            col = parent[static_cast<size_t>( r ) * width + col];
        --r;
    }
    if ( totalCost )
        *totalCost = best;
    return path;
}

std::vector<int> seamHorizontal( const std::vector<float> &cost, int width, int height,
                                 double *totalCost )
{
    // Transposed traversal: steps move along +x, seam picks a row per column.
    std::vector<double> acc( static_cast<size_t>( width ) * height, 0.0 );
    std::vector<int32_t> parent( static_cast<size_t>( width ) * height, 0 );
    for ( int r = 0; r < height; ++r )
        acc[static_cast<size_t>( r ) * width] = cost[static_cast<size_t>( r ) * width];

    for ( int c = 1; c < width; ++c )
    {
        for ( int r = 0; r < height; ++r )
        {
            const size_t idx = static_cast<size_t>( r ) * width + c;
            int bestPr = std::max( 0, r - 1 );
            double best = acc[static_cast<size_t>( bestPr ) * width + ( c - 1 )];
            for ( int pr = std::max( 0, r - 1 ) + 1; pr <= std::min( height - 1, r + 1 ); ++pr )
            {
                const double v = acc[static_cast<size_t>( pr ) * width + ( c - 1 )];
                if ( v < best )
                {
                    best = v;
                    bestPr = pr;
                }
            }
            acc[idx] = best + cost[idx];
            parent[idx] = bestPr;
        }
    }

    int row = 0;
    double best = acc[static_cast<size_t>( width - 1 )];
    for ( int r = 1; r < height; ++r )
    {
        const double v = acc[static_cast<size_t>( r ) * width + ( width - 1 )];
        if ( v < best )
        {
            best = v;
            row = r;
        }
    }

    std::vector<int> path( width );
    for ( int c = width - 1; c >= 0; --c )
    {
        path[c] = row;
        if ( c > 0 )
            row = parent[static_cast<size_t>( row ) * width + c];
    }
    if ( totalCost )
        *totalCost = best;
    return path;
}

} // namespace

std::vector<int> computeSeamPath( const std::vector<float> &cost, int width, int height,
                                  SeamOrientation orientation, double *totalCost )
{
    if ( totalCost )
        *totalCost = 0.0;
    if ( width <= 0 || height <= 0 ||
         cost.size() != static_cast<size_t>( width ) * height )
        return {};
    for ( float v : cost )
    {
        if ( !std::isfinite( v ) )
            return {};
    }
    if ( orientation == SeamOrientation::Vertical )
        return seamVertical( cost, width, height, totalCost );
    return seamHorizontal( cost, width, height, totalCost );
}

SeamDecision decideSeam( const std::vector<float> &cost, int width, int height,
                         SeamOrientation orientation, bool aOnNegativeSide )
{
    SeamDecision d;
    d.orientation = orientation;
    d.aSide = aOnNegativeSide ? +1 : -1;
    d.path = computeSeamPath( cost, width, height, orientation, &d.totalCost );
    return d;
}

BinnedSeamCost::BinnedSeamCost( int64_t overlapWidth, int64_t overlapHeight,
                                SeamCostWeights weights, int maxCells )
    : overlapW_( overlapWidth ), overlapH_( overlapHeight ), weights_( weights )
{
    const int64_t cap = std::max<int64_t>( 1, maxCells );
    cellsX_ = std::max<int64_t>( 1, std::min( overlapW_, cap ) );
    cellsY_ = std::max<int64_t>( 1, std::min( overlapH_, cap ) );
    cellW_ = ( overlapW_ + cellsX_ - 1 ) / cellsX_;
    cellH_ = ( overlapH_ + cellsY_ - 1 ) / cellsY_;
    cellsX_ = ( overlapW_ + cellW_ - 1 ) / cellW_;
    cellsY_ = ( overlapH_ + cellH_ - 1 ) / cellH_;
    const size_t n = static_cast<size_t>( cellsX_ ) * cellsY_;
    sumRad_.assign( n, 0.0 );
    sumGrad_.assign( n, 0.0 );
    sumCloud_.assign( n, 0.0 );
    sumEdge_.assign( n, 0.0 );
    count_.assign( n, 0 );
}

void BinnedSeamCost::addWindow( int64_t x, int64_t y, int64_t w, int64_t h,
                                const std::vector<float> &sceneA, const std::vector<float> &sceneB,
                                const std::vector<float> &cloudPenA,
                                const std::vector<float> &cloudPenB )
{
    if ( w <= 0 || h <= 0 || sceneA.size() != static_cast<size_t>( w ) * h ||
         sceneB.size() != sceneA.size() )
        return;
    const bool hasCloudA = !cloudPenA.empty();
    const bool hasCloudB = !cloudPenB.empty();

    // Normalizer for the edge-distance term: half of the shorter overlap side.
    const double halfMin = 0.5 * std::min<double>( overlapW_, overlapH_ );

    for ( int64_t r = 0; r < h; ++r )
    {
        const int64_t py = y + r;
        if ( py < 0 || py >= overlapH_ )
            continue;
        for ( int64_t c = 0; c < w; ++c )
        {
            const int64_t px = x + c;
            if ( px < 0 || px >= overlapW_ )
                continue;
            const size_t t = static_cast<size_t>( r ) * w + c;
            const float a = sceneA[t];
            const float b = sceneB[t];
            if ( !std::isfinite( a ) || !std::isfinite( b ) )
                continue;

            double rad = std::abs( static_cast<double>( a ) - b );

            // Forward-difference gradient disagreement within the window
            // (missing neighbors contribute 0 — a deterministic under-count
            // at window borders).
            double grad = 0.0;
            if ( c + 1 < w )
            {
                const float an = sceneA[t + 1];
                const float bn = sceneB[t + 1];
                if ( std::isfinite( an ) && std::isfinite( bn ) )
                    grad += std::abs( ( an - a ) - ( bn - b ) );
            }
            if ( r + 1 < h )
            {
                const float an = sceneA[t + w];
                const float bn = sceneB[t + w];
                if ( std::isfinite( an ) && std::isfinite( bn ) )
                    grad += std::abs( ( an - a ) - ( bn - b ) );
            }

            double cloud = 0.0;
            if ( hasCloudA && std::isfinite( cloudPenA[t] ) )
                cloud = std::max( cloud, static_cast<double>( cloudPenA[t] ) );
            if ( hasCloudB && std::isfinite( cloudPenB[t] ) )
                cloud = std::max( cloud, static_cast<double>( cloudPenB[t] ) );

            // Edge-distance penalty: cost is high *near the overlap border*
            // (where a seam would hug the footprint edge and leave no room
            // for the feather band) and zero mid-overlap.
            const double midness = std::min( { static_cast<double>( px ),
                                               static_cast<double>( py ),
                                               static_cast<double>( overlapW_ - 1 - px ),
                                               static_cast<double>( overlapH_ - 1 - py ) } ) /
                                   std::max( 1.0, halfMin );
            const double edge = std::clamp( 1.0 - midness, 0.0, 1.0 );

            const size_t cell = static_cast<size_t>( py / cellH_ ) * cellsX_ +
                                static_cast<size_t>( px / cellW_ );
            sumRad_[cell] += rad;
            sumGrad_[cell] += grad;
            sumCloud_[cell] += cloud;
            sumEdge_[cell] += edge;
            ++count_[cell];
        }
    }
}

SeamDecision BinnedSeamCost::solve()
{
    const size_t n = static_cast<size_t>( cellsX_ ) * cellsY_;
    std::vector<float> cost( n, kInvalidCellCost );
    for ( size_t i = 0; i < n; ++i )
    {
        if ( count_[i] == 0 )
            continue; // untouched/fully-invalid cell: keep the sentinel cost
        cost[i] = static_cast<float>(
            weights_.radiometric * ( sumRad_[i] / count_[i] ) +
            weights_.gradient * ( sumGrad_[i] / count_[i] ) +
            weights_.cloud * ( sumCloud_[i] / count_[i] ) +
            weights_.edgeDistance * ( sumEdge_[i] / count_[i] ) );
    }
    SeamDecision d;
    d.orientation = ( cellsX_ >= cellsY_ ) ? SeamOrientation::Vertical
                                           : SeamOrientation::Horizontal;
    // Side assignment is decided by the caller from placements; binned cost
    // itself is side-agnostic.
    d.aSide = +1;
    d.path = computeSeamPath( cost, static_cast<int>( cellsX_ ), static_cast<int>( cellsY_ ),
                              d.orientation, &d.totalCost );
    return d;
}

} // namespace rs::mosaic
