// sar_network_inversion.cpp — see sar_network_inversion.h
#include "sar_network_inversion.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sicnu::sar
{

namespace
{
constexpr int kMaxEpochs = 200;
// The pattern mask is a u64 over pairs — the honest pair bound (a stack
// with more pairs must be inverted with a fully-valid intersect mask
// upstream; the mask cannot express its per-pixel missing data).
constexpr int kMaxPairs = 64;
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

/// Dense SPD Cholesky: L·Lᵀ = A in place (row-major full buffer, lower
/// triangle used). Returns false on a non-positive pivot — a typed
/// rank-deficiency refusal, never a pseudo-inverse.
bool choleskyInPlace( std::vector<double> &a, size_t n )
{
    for ( size_t j = 0; j < n; ++j )
    {
        double d = a[j * n + j];
        for ( size_t k = 0; k < j; ++k )
            d -= a[j * n + k] * a[j * n + k];
        if ( !( d > 0.0 ) || !std::isfinite( d ) )
            return false;
        d = std::sqrt( d );
        a[j * n + j] = d;
        for ( size_t i = j + 1; i < n; ++i )
        {
            double v = a[i * n + j];
            for ( size_t k = 0; k < j; ++k )
                v -= a[i * n + k] * a[j * n + k];
            a[i * n + j] = v / d;
            a[j * n + i] = 0.0;
        }
    }
    return true;
}

/// Solves L·Lᵀ·x = b in place over the lower-triangular L (row-major).
void choleskySolve( const std::vector<double> &l, size_t n, std::vector<double> &b )
{
    for ( size_t i = 0; i < n; ++i )
    {
        for ( size_t k = 0; k < i; ++k )
            b[i] -= l[i * n + k] * b[k];
        b[i] /= l[i * n + i];
    }
    for ( size_t ii = 0; ii < n; ++ii )
    {
        const size_t i = n - 1 - ii;
        for ( size_t k = i + 1; k < n; ++k )
            b[i] -= l[k * n + i] * b[k];
        b[i] /= l[i * n + i];
    }
}
} // namespace

bool NetworkInversionProblem::isValid() const
{
    if ( epochCount < 2 || epochCount > kMaxEpochs )
        return false;
    if ( pairCount < 1 || pairCount > kMaxPairs )
        return false;
    if ( pairMasterEpoch.size() != static_cast<size_t>( pairCount )
         || pairSlaveEpoch.size() != static_cast<size_t>( pairCount )
         || pairTemporalYears.size() != static_cast<size_t>( pairCount ) )
        return false;
    if ( !pairWeights.empty() && pairWeights.size() != static_cast<size_t>( pairCount ) )
        return false;
    for ( int i = 0; i < pairCount; ++i )
    {
        const int m = pairMasterEpoch[static_cast<size_t>( i )];
        const int s = pairSlaveEpoch[static_cast<size_t>( i )];
        if ( m <= s || m < 1 || m >= epochCount || s < 0 )
            return false;
        if ( !std::isfinite( pairTemporalYears[static_cast<size_t>( i )] ) )
            return false;
        if ( !pairWeights.empty() )
        {
            const double w = pairWeights[static_cast<size_t>( i )];
            if ( !std::isfinite( w ) || w <= 0.0 )
                return false;
        }
    }
    return true;
}

NetworkInversionSolver::NetworkInversionSolver( const NetworkInversionProblem &problem,
                                                int maxPatterns )
    : m_problem( problem ), m_maxPatterns( maxPatterns )
{
}

const NetworkInversionSolver::PatternSolution &
NetworkInversionSolver::patternFor( uint64_t mask, QString *error, bool *ok )
{
    *ok = true;
    const auto it = m_patterns.find( mask );
    if ( it != m_patterns.end() )
        return it->second;

    if ( static_cast<long>( m_patterns.size() ) >= m_maxPatterns )
    {
        *ok = false;
        if ( error )
            *error = QStringLiteral(
                         "NETWORK_INVERSION_PATTERN_BLOWUP: %1 distinct missing-data "
                         "patterns exceeded the cache bound of %2 — switch the operator "
                         "to the intersect mask strategy (a pixel missing in ANY pair "
                         "is dropped) or raise the bound consciously" )
                         .arg( static_cast<qulonglong>( m_patterns.size() + 1 ) )
                         .arg( m_maxPatterns );
        static const PatternSolution kEmpty;
        return kEmpty;
    }

    const auto weightOf = [ this ]( int pair ) -> double {
        return m_problem.pairWeights.empty()
                   ? 1.0
                   : m_problem.pairWeights[static_cast<size_t>( pair )];
    };

    // Reference-component selection for this validity mask.
    std::vector<int> parent( static_cast<size_t>( m_problem.epochCount ) );
    std::iota( parent.begin(), parent.end(), 0 );
    const auto find = [&parent]( int x ) {
        while ( parent[static_cast<size_t>( x )] != x )
        {
            parent[static_cast<size_t>( x )] =
                parent[static_cast<size_t>( parent[static_cast<size_t>( x )] )];
            x = parent[static_cast<size_t>( x )];
        }
        return x;
    };
    std::vector<int> validPairs;
    for ( int i = 0; i < m_problem.pairCount; ++i )
    {
        if ( mask & ( 1ULL << i ) )
        {
            const int rm = find( m_problem.pairMasterEpoch[static_cast<size_t>( i )] );
            const int rs = find( m_problem.pairSlaveEpoch[static_cast<size_t>( i )] );
            if ( rm != rs )
                parent[static_cast<size_t>( std::max( rm, rs ) )] = std::min( rm, rs );
            validPairs.push_back( i );
        }
    }
    const int refRoot = find( 0 );
    std::vector<int> epochColumn( static_cast<size_t>( m_problem.epochCount ), -1 );
    PatternSolution solution;
    for ( int e = 1; e < m_problem.epochCount; ++e )
        if ( find( e ) == refRoot )
        {
            epochColumn[static_cast<size_t>( e )] =
                static_cast<int>( solution.solvedEpochs.size() );
            solution.solvedEpochs.push_back( e );
        }
    for ( const int i : validPairs )
        if ( find( m_problem.pairMasterEpoch[static_cast<size_t>( i )] ) == refRoot )
            solution.keptPairs.push_back( i );

    const size_t dim = solution.solvedEpochs.size();
    solution.dim = dim;

    if ( dim > 0 && !solution.keptPairs.empty() )
    {
        // A = GᵀWG over the kept rows; G has +1 at the master's column and
        // −1 at the slave's column per row.
        std::vector<double> a( dim * dim, 0.0 );
        for ( const int pair : solution.keptPairs )
        {
            const double w = weightOf( pair );
            const int mc = epochColumn[static_cast<size_t>(
                m_problem.pairMasterEpoch[static_cast<size_t>( pair )] )];
            const int sc = epochColumn[static_cast<size_t>(
                m_problem.pairSlaveEpoch[static_cast<size_t>( pair )] )];
            // Epochs outside the solved set (the reference epoch 0 - its
            // displacement is identically 0 - and any non-component epoch)
            // contribute NO column: their +/-1 terms drop out of the
            // reduced normal equations. epochColumn is -1 there.
            if ( mc >= 0 )
                a[static_cast<size_t>( mc ) * dim + mc] += w;
            if ( sc >= 0 )
                a[static_cast<size_t>( sc ) * dim + sc] += w;
            if ( mc >= 0 && sc >= 0 )
            {
                a[static_cast<size_t>( mc ) * dim + sc] -= w;
                a[static_cast<size_t>( sc ) * dim + mc] -= w;
            }
        }
        if ( !choleskyInPlace( a, dim ) )
        {
            *ok = false;
            if ( error )
                *error = QStringLiteral(
                             "NETWORK_INVERSION_RANK_DEFICIENT: the epoch system of "
                             "validity pattern %1 is rank-deficient (degenerate weights "
                             "or a broken sub-network) — refusing instead of a "
                             "pseudo-inverse" )
                             .arg( static_cast<qulonglong>( mask ) );
            static const PatternSolution kEmpty2;
            return kEmpty2;
        }
        solution.chol = std::move( a );
    }

    return m_patterns.emplace( mask, std::move( solution ) ).first->second;
}

bool NetworkInversionSolver::solvePixel( const double *displacement,
                                         const double *epochTemporalYears,
                                         double *epochDisplacement, double *velocityMPerYear,
                                         double *rmsResidualM, int *solvedEpochs,
                                         int *droppedPairs, QString *error )
{
    if ( !m_problem.isValid() || !displacement || !epochDisplacement )
    {
        if ( error )
            *error = QStringLiteral( "NETWORK_INVERSION_RANK_DEFICIENT: invalid problem "
                                     "contract or null buffers" );
        return false;
    }

    uint64_t mask = 0;
    for ( int i = 0; i < m_problem.pairCount; ++i )
        if ( std::isfinite( displacement[i] ) )
            mask |= ( 1ULL << i );

    bool ok = false;
    const PatternSolution &pattern = patternFor( mask, error, &ok );
    if ( !ok )
        return false;

    for ( int e = 0; e < m_problem.epochCount; ++e )
        epochDisplacement[e] = kNan;
    epochDisplacement[0] = 0.0;
    if ( solvedEpochs )
        *solvedEpochs = static_cast<int>( pattern.solvedEpochs.size() );
    if ( rmsResidualM )
        *rmsResidualM = kNan;
    if ( velocityMPerYear )
        *velocityMPerYear = kNan;
    if ( droppedPairs )
        *droppedPairs = m_problem.pairCount - static_cast<int>( pattern.keptPairs.size() );

    const size_t dim = pattern.dim;
    if ( dim == 0 || pattern.keptPairs.empty() )
        return true; // nothing reachable from the reference — honest NaNs

    // b = Gᵀ·W·d over the kept rows (weights are problem-level).
    const auto weightOf = [this]( int pair ) -> double {
        return m_problem.pairWeights.empty()
                   ? 1.0
                   : m_problem.pairWeights[static_cast<size_t>( pair )];
    };
    std::vector<double> b( dim, 0.0 );
    for ( const int pair : pattern.keptPairs )
    {
        const auto it_m = std::find( pattern.solvedEpochs.begin(),
                                     pattern.solvedEpochs.end(),
                                     m_problem.pairMasterEpoch[static_cast<size_t>( pair )] );
        const auto it_s = std::find( pattern.solvedEpochs.begin(),
                                     pattern.solvedEpochs.end(),
                                     m_problem.pairSlaveEpoch[static_cast<size_t>( pair )] );
        const double wd = weightOf( pair ) * displacement[pair];
        // A reference-component row whose slave/master is the reference
        // epoch (index not in solvedEpochs) contributes to ONE column only:
        // the reference epoch's displacement is identically 0.
        if ( it_m != pattern.solvedEpochs.end() )
            b[static_cast<size_t>( it_m - pattern.solvedEpochs.begin() )] += wd;
        if ( it_s != pattern.solvedEpochs.end() )
            b[static_cast<size_t>( it_s - pattern.solvedEpochs.begin() )] -= wd;
    }

    std::vector<double> u = b;
    choleskySolve( pattern.chol, dim, u );
    for ( size_t c = 0; c < dim; ++c )
        epochDisplacement[pattern.solvedEpochs[c]] = u[c];

    if ( velocityMPerYear )
    {
        double sumT = 0.0, sumU = 0.0, sumTT = 0.0, sumTU = 0.0;
        for ( size_t c = 0; c < dim; ++c )
        {
            const double t =
                epochTemporalYears
                    ? epochTemporalYears[pattern.solvedEpochs[c]]
                    : static_cast<double>( pattern.solvedEpochs[c] );
            sumT += t;
            sumU += u[c];
            sumTT += t * t;
            sumTU += t * u[c];
        }
        const double denom = static_cast<double>( dim ) * sumTT - sumT * sumT;
        if ( std::abs( denom ) > 1e-30 )
            *velocityMPerYear =
                ( static_cast<double>( dim ) * sumTU - sumT * sumU ) / denom;
    }

    if ( rmsResidualM )
    {
        double sumSq = 0.0;
        long rows = 0;
        for ( const int pair : pattern.keptPairs )
        {
            const double um =
                epochDisplacement[m_problem.pairMasterEpoch[static_cast<size_t>( pair )]];
            const double us =
                epochDisplacement[m_problem.pairSlaveEpoch[static_cast<size_t>( pair )]];
            if ( !std::isfinite( um ) || !std::isfinite( us ) )
                continue;
            const double residual = displacement[pair] - ( um - us );
            sumSq += residual * residual;
            ++rows;
        }
        if ( rows > 0 )
            *rmsResidualM = std::sqrt( sumSq / rows );
    }
    return true;
}

} // namespace sicnu::sar
