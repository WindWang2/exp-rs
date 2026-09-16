// sar_phase_closure.cpp — see sar_phase_closure.h
#include "sar_phase_closure.h"

#include "sar_insar.h"

#include <cmath>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
} // namespace

double phaseClosureRad( std::complex<double> ifgAB, std::complex<double> ifgBC,
                        std::complex<double> ifgCA )
{
    const double p1 = interferogramPhase( ifgAB, { 1.0, 0.0 } );
    const double p2 = interferogramPhase( ifgBC, { 1.0, 0.0 } );
    const double p3 = interferogramPhase( ifgCA, { 1.0, 0.0 } );
    if ( !std::isfinite( p1 ) || !std::isfinite( p2 ) || !std::isfinite( p3 ) )
        return kNaN;
    double closure = p1 + p2 + p3;
    // Wrap into (−π, π]; the exact closure is 0, the round-off residue is
    // within one π of it for any sane stack.
    closure = std::fmod( closure + M_PI, 2.0 * M_PI );
    if ( closure < 0.0 )
        closure += 2.0 * M_PI;
    return closure - M_PI;
}

void phaseClosurePlane( const std::complex<float> *ifgAB, const std::complex<float> *ifgBC,
                        const std::complex<float> *ifgCA, int w, int h,
                        double *closureOut, PhaseClosureStats *stats,
                        const std::function<void()> &cancelProbe )
{
    PhaseClosureStats local;
    if ( stats )
        *stats = local;
    if ( !ifgAB || !ifgBC || !ifgCA || w <= 0 || h <= 0 )
        return;

    double sumSq = 0.0;
    const long long n = static_cast<long long>( w ) * h;
    for ( long long i = 0; i < n; ++i )
    {
        if ( cancelProbe && ( i % ( 64LL * w ) ) == 0 )
            cancelProbe();
        ++local.evaluated;
        const double closure = phaseClosureRad( ifgAB[i], ifgBC[i], ifgCA[i] );
        if ( closureOut )
            closureOut[i] = closure;
        if ( std::isfinite( closure ) )
        {
            ++local.validCount;
            const double absClosure = std::abs( closure );
            local.maxAbsClosureRad = std::max( local.maxAbsClosureRad, absClosure );
            sumSq += closure * closure;
        }
        else
        {
            ++local.invalidCount;
        }
    }
    if ( local.validCount > 0 )
        local.rmsClosureRad = std::sqrt( sumSq / local.validCount );
    if ( stats )
        *stats = local;
}

} // namespace sicnu::sar
