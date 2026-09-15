// mosaic_balancing.cpp — F15 Package B implementation (ADR 0163).
#include "mosaic_balancing.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <numeric>

namespace rs::mosaic {
namespace {

struct LsqAccum {
    int64_t n = 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;

    void add( double x, double y )
    {
        ++n;
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
        syy += y * y;
    }
    bool solve( double *gain, double *bias ) const
    {
        if ( n < 2 )
            return false;
        const double denom = static_cast<double>( n ) * sxx - sx * sx;
        const double meanX = sx / n;
        // Degenerate overlap (constant x) carries no gain information.
        if ( std::abs( denom ) <= 1e-12 * std::max( 1.0, std::abs( meanX ) ) )
            return false;
        const double g = ( static_cast<double>( n ) * sxy - sx * sy ) / denom;
        const double b = ( sy - g * sx ) / n;
        if ( !std::isfinite( g ) || !std::isfinite( b ) )
            return false;
        *gain = g;
        *bias = b;
        return true;
    }
};

// Streaming window visitor over a pair intersection rectangle in grid coords.
// Calls fn(x0, y0, w, h) for each bounded sub-window.
template <typename Fn>
bool forEachWindow( int64_t x0, int64_t y0, int64_t w, int64_t h, int windowSize, Fn &&fn )
{
    for ( int64_t y = 0; y < h; y += windowSize )
    {
        const int64_t hh = std::min<int64_t>( windowSize, h - y );
        for ( int64_t x = 0; x < w; x += windowSize )
        {
            const int64_t ww = std::min<int64_t>( windowSize, w - x );
            if ( !fn( x0 + x, y0 + y, ww, hh ) )
                return false;
        }
    }
    return true;
}

struct PairRect
{
    int64_t x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
};

PairRect overlapRect( const MosaicPlan &plan, int ia, int ib )
{
    const ScenePlacement &pa = plan.scenes[ia].placement;
    const ScenePlacement &pb = plan.scenes[ib].placement;
    const int64_t ix0 = std::max<int64_t>( pa.offsetX, pb.offsetX );
    const int64_t iy0 = std::max<int64_t>( pa.offsetY, pb.offsetY );
    const int64_t ix1 = std::min<int64_t>( pa.offsetX + plan.scenes[ia].scene.width,
                                           pb.offsetX + plan.scenes[ib].scene.width );
    const int64_t iy1 = std::min<int64_t>( pa.offsetY + plan.scenes[ia].scene.height,
                                           pb.offsetY + plan.scenes[ib].scene.height );
    PairRect r;
    r.x = ix0;
    r.y = iy0;
    r.w = ix1 - ix0;
    r.h = iy1 - iy0;
    return r;
}

// Deterministic bounded reservoir for residual MAD: keep every k-th value so
// the subsample is a fixed function of the stream order (no RNG, no growth).
struct StridedResiduals
{
    std::vector<float> values;
    size_t seen = 0;
    static constexpr size_t kCap = 65536;
    void add( double r )
    {
        ++seen;
        if ( values.size() < kCap )
        {
            values.push_back( static_cast<float>( r ) );
        }
        else
        {
            // Reservoir-style stride: once full, keep a deterministic
            // arithmetic subsample of the remaining stream.
            const size_t stride = ( seen - 1 ) / kCap + 1;
            if ( ( seen - 1 ) % stride == 0 )
                values[( seen - 1 ) / stride % kCap] = static_cast<float>( r );
        }
    }
    double mad() const
    {
        if ( values.empty() )
            return 0.0;
        std::vector<float> sorted( values );
        std::sort( sorted.begin(), sorted.end() );
        const size_t mid = sorted.size() / 2;
        double med = sorted[mid];
        if ( sorted.size() % 2 == 0 )
            med = 0.5 * ( sorted[mid - 1] + sorted[mid] );
        std::vector<double> dev( sorted.size() );
        for ( size_t i = 0; i < sorted.size(); ++i )
            dev[i] = std::abs( static_cast<double>( sorted[i] ) - med );
        std::sort( dev.begin(), dev.end() );
        const size_t m = dev.size() / 2;
        return dev.size() % 2 == 1 ? dev[m] : 0.5 * ( dev[m - 1] + dev[m] );
    }
};

// Deterministic bounded paired-sample store for the Theil-Sen seed:
// keep every k-th (x, y) pair so the subsample is a fixed function of the
// stream order (no RNG, no unbounded growth).
struct StridedPairs
{
    std::vector<double> xs, ys;
    size_t seen = 0;
    static constexpr size_t kCap = 65536;
    void add( double x, double y )
    {
        ++seen;
        if ( xs.size() < kCap )
        {
            xs.push_back( x );
            ys.push_back( y );
        }
        else
        {
            const size_t stride = ( seen - 1 ) / kCap + 1;
            if ( ( seen - 1 ) % stride == 0 )
            {
                const size_t idx = ( seen - 1 ) / stride % kCap;
                xs[idx] = x;
                ys[idx] = y;
            }
        }
    }
};

// LMS (least-median-of-squares) seed, RANSAC-style with deterministic
// candidates: candidate lines through pairs of a strided point subsample are
// scored by the median of squared residuals over a strided evaluation
// subsample. Tolerates >40% clustered contamination (cloud blocks) that
// tilts a plain LSQ; the caller refines the seed with MAD trims. All
// subsampling is stride-based: fully deterministic, bounded memory/work.
void lmsSeed( const StridedPairs &sample, double *gain, double *bias )
{
    const size_t n = sample.xs.size();
    if ( n < 8 )
        return; // caller keeps its LSQ fit

    auto strided = []( const std::vector<double> &v, size_t count ) {
        std::vector<double> out;
        const size_t stride = std::max<size_t>( 1, v.size() / std::max<size_t>( 1, count ) );
        for ( size_t i = 0; i < v.size() && out.size() < count; i += stride )
            out.push_back( v[i] );
        return out;
    };

    const std::vector<double> px = strided( sample.xs, 64 );
    const std::vector<double> py = strided( sample.ys, 64 );
    const std::vector<double> ex = strided( sample.xs, 1024 );
    const std::vector<double> ey = strided( sample.ys, 1024 );
    if ( px.size() < 2 || ex.size() < 8 )
        return;

    std::vector<double> residuals;
    residuals.reserve( ex.size() );
    double bestMedian = std::numeric_limits<double>::max();
    double bestA = 0.0, bestB = 0.0;
    bool found = false;

    for ( size_t i = 0; i + 1 < px.size(); ++i )
        for ( size_t j = i + 1; j < px.size(); ++j )
        {
            const double dx = px[j] - px[i];
            if ( std::abs( dx ) <= 1e-9 * std::max( 1.0, std::abs( px[i] ) ) )
                continue; // vertical candidate carries no slope information
            const double a = ( py[j] - py[i] ) / dx;
            if ( !std::isfinite( a ) || a <= 0.0 )
                continue; // radiometric gain must be positive
            const double b = py[i] - a * px[i];
            residuals.clear();
            for ( size_t t = 0; t < ex.size(); ++t )
                residuals.push_back( std::abs( ey[t] - ( a * ex[t] + b ) ) );
            std::nth_element( residuals.begin(), residuals.begin() + residuals.size() / 2,
                              residuals.end() );
            // median of SQUARED residuals == square of the median of |r|
            const double med = residuals[residuals.size() / 2];
            const double score = med * med;
            if ( score < bestMedian )
            {
                bestMedian = score;
                bestA = a;
                bestB = b;
                found = true;
            }
        }

    if ( found )
    {
        *gain = bestA;
        *bias = bestB;
    }
}

} // namespace

bool RadiometricBalancer::balance( const MosaicPlan &plan, OverlapSampler &sampler,
                                   int bandCount, const BalancingOptions &options,
                                   std::vector<SceneBalance> *out, std::string *errorMessage )
{
    auto fail = [errorMessage]( const std::string &m ) {
        if ( errorMessage )
            *errorMessage = m;
        return false;
    };
    if ( !out || bandCount <= 0 )
        return fail( "RadiometricBalancer: invalid arguments" );

    const int n = static_cast<int>( plan.scenes.size() );
    out->clear();
    out->resize( n );
    for ( int i = 0; i < n; ++i )
    {
        SceneBalance &b = ( *out )[i];
        b.scene = i;
        b.parentScene = -1;
        b.perBand.assign( bandCount, BalanceGain{} );
    }

    // Reference: explicit, else largest grid-eligible area, tie -> lowest index.
    int ref = options.referenceScene;
    if ( ref < 0 )
    {
        for ( int i = 0; i < n; ++i )
        {
            if ( !plan.scenes[i].gridEligible )
                continue;
            if ( ref < 0 )
            {
                ref = i;
                continue;
            }
            const int64_t areaI = static_cast<int64_t>( plan.scenes[i].scene.width ) *
                                  plan.scenes[i].scene.height;
            const int64_t areaR = static_cast<int64_t>( plan.scenes[ref].scene.width ) *
                                  plan.scenes[ref].scene.height;
            if ( areaI > areaR )
                ref = i;
        }
    }
    if ( ref < 0 || !plan.scenes[ref].gridEligible )
        return fail( "RadiometricBalancer: no grid-eligible reference scene available" );
    ( *out )[ref].referenceHop = 0;
    ( *out )[ref].parentScene = ref;

    // BFS through the overlap graph in deterministic plan order.
    std::vector<bool> visited( n, false );
    visited[ref] = true;
    std::deque<int> queue { ref };

    // Pre-index overlaps by scene for neighbor iteration in plan order.
    std::vector<int> rejectList;

    while ( !queue.empty() )
    {
        const int parent = queue.front();
        queue.pop_front();
        const SceneBalance &parentBalance = ( *out )[parent];

        for ( const OverlapPair &pair : plan.overlaps )
        {
            if ( pair.a != parent && pair.b != parent )
                continue;
            const int child = pair.a == parent ? pair.b : pair.a;
            if ( visited[child] )
                continue;
            if ( !plan.scenes[child].gridEligible )
                continue;

            const PairRect rect = overlapRect( plan, parent, child );
            if ( rect.empty() || rect.w * rect.h < options.minOverlapPixels )
                continue;

            // Per-band robust fit: corrected(parent) ≈ α·raw(child) + β.
            std::vector<BalanceGain> fits( bandCount );
            std::vector<int64_t> support( bandCount, 0 );
            std::vector<double> fitStdY( bandCount, 0.0 );
            std::vector<double> fitMeanY( bandCount, 0.0 );
            bool fitOk = true;
            std::string fitError;

            for ( int band = 0; band < bandCount && fitOk; ++band )
            {
                // Pass 1: least squares over the overlap + deterministic
                // paired subsample for the Theil-Sen seed.
                LsqAccum acc;
                StridedPairs sample;
                if ( !forEachWindow( rect.x, rect.y, rect.w, rect.h, options.windowSize,
                                     [&]( int64_t wx, int64_t wy, int64_t ww, int64_t wh ) {
                                         std::vector<float> pv, cv;
                                         if ( !sampler.readGridWindow( parent, band, wx, wy, ww, wh, pv ) ||
                                              !sampler.readGridWindow( child, band, wx, wy, ww, wh, cv ) )
                                             return false;
                                         const size_t k = pv.size();
                                         for ( size_t t = 0; t < k; ++t )
                                         {
                                             const double x = cv[t];
                                             const double y = parentBalance.perBand[band].apply( pv[t] );
                                             if ( std::isfinite( x ) && std::isfinite( y ) )
                                             {
                                                 acc.add( x, y );
                                                 sample.add( x, y );
                                             }
                                         }
                                         return true;
                                     } ) )
                {
                    fitError = "sampler read failure on overlap of scenes " +
                               std::to_string( parent ) + "/" + std::to_string( child );
                    fitOk = false;
                    break;
                }

                double g = 0, b0 = 0;
                if ( static_cast<int64_t>( acc.n ) < options.minOverlapPixels || !acc.solve( &g, &b0 ) )
                    continue; // unusable overlap for this band: leave identity, 0 support
                // Robust seed: least-median-of-squares survives clustered
                // contamination (e.g. cloud blocks) that tilts the plain LSQ.
                lmsSeed( sample, &g, &b0 );

                {
                    const double meanY = acc.sy / acc.n;
                    fitStdY[band] = std::sqrt( std::max( 0.0, acc.syy / acc.n - meanY * meanY ) );
                    fitMeanY[band] = meanY;
                }

                // Iterative robust refinement (deterministic strided residual
                // subsample): residuals vs the current fit -> MAD -> inlier
                // refit, repeated so heavy clustered contamination (cloud
                // blocks) is fully trimmed even when it tilted the first LSQ.
                int64_t inlierSupport = 0;
                for ( int iter = 0; iter < 2; ++iter )
                {
                    StridedResiduals residuals;
                    if ( !forEachWindow( rect.x, rect.y, rect.w, rect.h, options.windowSize,
                                         [&]( int64_t wx, int64_t wy, int64_t ww, int64_t wh ) {
                                             std::vector<float> pv, cv;
                                             if ( !sampler.readGridWindow( parent, band, wx, wy, ww, wh, pv ) ||
                                                  !sampler.readGridWindow( child, band, wx, wy, ww, wh, cv ) )
                                                 return false;
                                             const size_t k = pv.size();
                                             for ( size_t t = 0; t < k; ++t )
                                             {
                                                 const double x = cv[t];
                                                 const double y = parentBalance.perBand[band].apply( pv[t] );
                                                 if ( std::isfinite( x ) && std::isfinite( y ) )
                                                     residuals.add( y - ( g * x + b0 ) );
                                             }
                                             return true;
                                         } ) )
                    {
                        fitError = "sampler read failure on overlap of scenes " +
                                   std::to_string( parent ) + "/" + std::to_string( child );
                        fitOk = false;
                        break;
                    }
                    const double sigma = 1.4826 * residuals.mad();

                    LsqAccum inlier;
                    if ( !forEachWindow( rect.x, rect.y, rect.w, rect.h, options.windowSize,
                                         [&]( int64_t wx, int64_t wy, int64_t ww, int64_t wh ) {
                                             std::vector<float> pv, cv;
                                             if ( !sampler.readGridWindow( parent, band, wx, wy, ww, wh, pv ) ||
                                                  !sampler.readGridWindow( child, band, wx, wy, ww, wh, cv ) )
                                                 return false;
                                             const size_t k = pv.size();
                                             for ( size_t t = 0; t < k; ++t )
                                             {
                                                 const double x = cv[t];
                                                 const double y = parentBalance.perBand[band].apply( pv[t] );
                                                 if ( std::isfinite( x ) && std::isfinite( y ) &&
                                                      std::abs( y - ( g * x + b0 ) ) <=
                                                          options.inlierK * sigma + 1e-12 )
                                                     inlier.add( x, y );
                                             }
                                             return true;
                                         } ) )
                    {
                        fitError = "sampler read failure on overlap of scenes " +
                                   std::to_string( parent ) + "/" + std::to_string( child );
                        fitOk = false;
                        break;
                    }

                    if ( static_cast<int64_t>( inlier.n ) >= options.minOverlapPixels )
                    {
                        double rg = 0, rb = 0;
                        if ( inlier.solve( &rg, &rb ) && rg > 0.0 )
                        {
                            const bool converged = ( inlier.n == acc.n );
                            g = rg;
                            b0 = rb;
                            inlierSupport = inlier.n;
                            if ( converged )
                                break; // clean overlap: refit == LSQ, no second pass needed
                        }
                        else
                            break;
                    }
                    else
                        break; // keep the previous fit
                }

                if ( g > 0.0 )
                {
                    fits[band] = { g, b0 };
                    support[band] = inlierSupport > 0 ? inlierSupport : acc.n;
                }
            }

            if ( !fitOk )
                return fail( "RadiometricBalancer: " + fitError );

            // Anomaly gates on the cumulative correction.
            bool reject = false;
            std::string reason;
            for ( int band = 0; band < bandCount; ++band )
            {
                const double totalGain = fits[band].gain;
                if ( totalGain < options.minGain || totalGain > options.maxGain )
                {
                    reject = true;
                    reason = "cumulative gain " + std::to_string( totalGain ) + " on band " +
                             std::to_string( band ) + " outside [" +
                             std::to_string( options.minGain ) + ", " +
                             std::to_string( options.maxGain ) + "]";
                    break;
                }
                // Bias gate relative to the overlap signal level: a
                // correction offset comparable to the data itself (several
                // times the mean level) is an anomaly; a moderate bias on a
                // low-contrast overlap is legitimate.
                if ( std::abs( fits[band].bias ) >
                     options.maxAbsBiasSigma *
                         ( std::abs( fitMeanY[band] ) + fitStdY[band] + 1e-12 ) )
                {
                    reject = true;
                    reason = "bias offset " + std::to_string( fits[band].bias ) + " on band " +
                             std::to_string( band ) + " exceeds " +
                             std::to_string( options.maxAbsBiasSigma ) +
                             "x the overlap signal level";
                    break;
                }
            }
            if ( !reject &&
                 *std::min_element( support.begin(), support.end() ) < options.minOverlapPixels )
            {
                reject = true;
                reason = "overlap support below threshold";
            }

            if ( reject )
            {
                SceneBalance &cb = ( *out )[child];
                cb.parentScene = parent;
                cb.referenceHop = parentBalance.referenceHop + 1;
                cb.supportPixels = *std::min_element( support.begin(), support.end() );
                cb.rejected = true;
                cb.rejectReason = reason;
                visited[child] = true; // do not revisit through other paths
                rejectList.push_back( child );
                continue;
            }

            SceneBalance &cb = ( *out )[child];
            cb.parentScene = parent;
            cb.referenceHop = parentBalance.referenceHop + 1;
            cb.perBand = fits;
            cb.supportPixels = *std::min_element( support.begin(), support.end() );
            visited[child] = true;
            queue.push_back( child );
        }
    }

    // Unreachable or ineligible scenes are rejected with reasons.
    for ( int i = 0; i < n; ++i )
    {
        SceneBalance &b = ( *out )[i];
        if ( i == ref || b.parentScene >= 0 )
            continue;
        b.rejected = true;
        b.rejectReason = plan.scenes[i].gridEligible
                             ? "no usable overlap path to the reference scene"
                             : "scene is not grid-eligible (CRS/pixel-size/rotation)";
        rejectList.push_back( i );
    }

    if ( options.rejectPolicy == BalancingOptions::RejectPolicy::Fail && !rejectList.empty() )
    {
        std::string names;
        for ( size_t i = 0; i < rejectList.size(); ++i )
        {
            if ( i )
                names += ", ";
            names += std::to_string( rejectList[i] ) + " (" + ( *out )[rejectList[i]].rejectReason + ")";
        }
        return fail( "RadiometricBalancer: scene(s) rejected — " + names +
                     "; set rejectPolicy=drop to proceed without them, or pre-correct the inputs" );
    }

    if ( errorMessage )
        errorMessage->clear();
    return true;
}

} // namespace rs::mosaic
