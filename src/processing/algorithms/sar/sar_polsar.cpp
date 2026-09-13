// src/processing/algorithms/sar/sar_polsar.cpp — PolSAR decomposition
// kernels. Model conventions are defined (and pinned by tests) in
// sar_polsar.h; this file implements exactly those matrices.
#include "sar_polsar.h"

#include "sar_hermitian3.h"

#include <cmath>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool finiteSample( const std::complex<double> &v )
{
    return std::isfinite( v.real() ) && std::isfinite( v.imag() );
}

/// Shared surface/double branch solve (Freeman-Durden step 4). Takes the
/// volume- and helix-reduced residuals; fills fs/fd and their SPAN powers
/// (Ps = fs·(1+|β|²) in the surface branch where α = 0; symmetric for the
/// double branch).
struct SurfaceDoubleSplit
{
    double fs = 0.0;
    double fd = 0.0;
    double ps = 0.0;
    double pd = 0.0;
};

SurfaceDoubleSplit solveSurfaceDouble( double r11, double r33, std::complex<double> r13 )
{
    SurfaceDoubleSplit s;
    if ( r13.real() >= 0.0 )
    {
        // Surface branch: β = conj(r13)/fs, α = 0.
        s.fs = r11;
        s.fd = ( s.fs > 0.0 ) ? r33 - std::norm( r13 ) / s.fs : r33;
        if ( s.fd < 0.0 )
            s.fd = 0.0;
        s.ps = s.fs > 0.0 ? s.fs + std::norm( r13 ) / s.fs : 0.0;
        s.pd = s.fd;
    }
    else
    {
        // Double-bounce branch: α = r13/fd, β = 0.
        s.fd = r33;
        s.fs = ( s.fd > 0.0 ) ? r11 - std::norm( r13 ) / s.fd : r11;
        if ( s.fs < 0.0 )
            s.fs = 0.0;
        s.pd = s.fd > 0.0 ? s.fd + std::norm( r13 ) / s.fd : 0.0;
        s.ps = s.fs;
    }
    return s;
}
} // anonymous namespace

void accumulatePolSample( PolEnsemble3 &ens, std::complex<double> shh,
                          std::complex<double> shv, std::complex<double> svv )
{
    if ( !finiteSample( shh ) || !finiteSample( shv ) || !finiteSample( svv ) )
        return;
    ens.samples++;
    ens.c11 += std::norm( shh );
    ens.c22 += std::norm( shv );
    ens.c33 += std::norm( svv );
    ens.c12 += shh * std::conj( shv );
    ens.c13 += shh * std::conj( svv );
    ens.c23 += shv * std::conj( svv );
}

bool finalizeEnsemble( const PolEnsemble3 &ens, PolEnsemble3 *covariance )
{
    if ( !covariance || ens.samples <= 0 )
        return false;
    const double n = static_cast<double>( ens.samples );
    *covariance = ens;
    covariance->samples = ens.samples;
    covariance->c11 /= n;
    covariance->c22 /= n;
    covariance->c33 /= n;
    covariance->c12 /= n;
    covariance->c13 /= n;
    covariance->c23 /= n;
    return true;
}

PauliPowers pauliPowers( std::complex<double> shh, std::complex<double> shv,
                         std::complex<double> svv )
{
    PauliPowers p;
    if ( !finiteSample( shh ) || !finiteSample( shv ) || !finiteSample( svv ) )
    {
        p.odd = p.doubleBounce = p.volume = p.span = kNaN;
        return p;
    }
    const std::complex<double> k1 = ( shh + svv ) / std::sqrt( 2.0 );
    const std::complex<double> k2 = ( shh - svv ) / std::sqrt( 2.0 );
    const std::complex<double> k3 = std::sqrt( 2.0 ) * shv;
    p.odd = std::norm( k1 );
    p.doubleBounce = std::norm( k2 );
    p.volume = std::norm( k3 );
    p.span = p.odd + p.doubleBounce + p.volume;
    return p;
}

Coherency3 coherencyFromCovariance( const PolEnsemble3 &cov )
{
    Coherency3 t;
    t.t11 = ( cov.c11 + cov.c33 + 2.0 * cov.c13.real() ) / 2.0;
    t.t22 = ( cov.c11 + cov.c33 - 2.0 * cov.c13.real() ) / 2.0;
    t.t33 = 2.0 * cov.c22;
    t.t12 = { ( cov.c11 - cov.c33 ) / 2.0, -cov.c13.imag() };
    // k1·k3* = (SHH+SVV)·SHV* → C12 + conj(C23); k2·k3* → C12 − conj(C23).
    t.t13 = cov.c12 + std::conj( cov.c23 );
    t.t23 = cov.c12 - std::conj( cov.c23 );
    return t;
}

bool cloudePottier( const PolEnsemble3 &cov, HAlphaResult *out )
{
    if ( !out )
        return false;
    const Coherency3 t = coherencyFromCovariance( cov );

    HermitianEigen3 e;
    if ( !hermitianEigen3( t.t11, t.t22, t.t33, t.t12, t.t13, t.t23, &e ) )
        return false;

    *out = HAlphaResult{};
    out->lambda[0] = e.lambda[0];
    out->lambda[1] = e.lambda[1];
    out->lambda[2] = e.lambda[2];

    const double total = e.lambda[0] + e.lambda[1] + e.lambda[2];
    if ( !( total > 0.0 ) )
    {
        // Zero power: undefined statistics, honest NaN.
        out->entropy = out->alphaMean = out->anisotropy = out->dominance = kNaN;
        return true;
    }

    // Eigen stability + entropy.
    double h = 0.0;
    double alphaMean = 0.0;
    const double ln3 = std::log( 3.0 );
    for ( int i = 0; i < 3; ++i )
    {
        const double pi = e.lambda[i] / total;
        if ( pi > 0.0 )
            h -= pi * std::log( pi ) / ln3;
        // α_i = arccos(|first component|) of the unit eigenvector.
        double norm = 0.0;
        for ( int k = 0; k < 3; ++k )
            norm += std::norm( e.vec[i][k] );
        const double absFirst = std::abs( e.vec[i][0] )
                                / ( norm > 0.0 ? std::sqrt( norm ) : 1.0 );
        const double alphaI = std::acos( std::min( 1.0, absFirst ) ) * 180.0 / M_PI;
        alphaMean += pi * alphaI;
    }
    out->entropy = h;
    out->alphaMean = alphaMean;
    out->dominance = e.lambda[0] / total;

    const double denom = e.lambda[1] + e.lambda[2];
    out->anisotropy = denom > 0.0 ? ( e.lambda[1] - e.lambda[2] ) / denom : kNaN;
    return true;
}

bool freemanDurden( const PolEnsemble3 &cov, FreemanDurdenResult *out )
{
    if ( !out )
        return false;
    *out = FreemanDurdenResult{};

    // Volume first (fv = 1.5·C22 under the Cv convention).
    const double fv = 1.5 * cov.c22;
    double r11 = cov.c11 - fv;
    double r33 = cov.c33 - fv;
    const std::complex<double> r13 = cov.c13 - fv / 3.0;

    // Clamp negative residuals (noise-floor overshoot): documented SPAN
    // break — the clamp keeps the model powers physical.
    if ( r11 < 0.0 )
        r11 = 0.0;
    if ( r33 < 0.0 )
        r33 = 0.0;

    const SurfaceDoubleSplit split = solveSurfaceDouble( r11, r33, r13 );

    out->ok = true;
    out->surface = split.ps;
    out->doubleBounce = split.pd;
    out->volume = ( 8.0 / 3.0 ) * fv;
    return true;
}

bool yamaguchi4( const PolEnsemble3 &cov, YamaguchiResult *out )
{
    if ( !out )
        return false;
    *out = YamaguchiResult{};

    // 1. Helix first: the helix model (rank-1 circular target, see header)
    //    contributes purely imaginary C12 = C23 = −j·fh/4 → fh = −4·mean.
    const double fh = -2.0 * ( cov.c12.imag() + cov.c23.imag() );

    // 2. Volume from the helix-decontaminated cross-pol power (the helix
    //    model's own |SHV|² = fh/4 must not inflate fv).
    const double fv = 1.5 * ( cov.c22 - fh / 4.0 );

    // 3. Doubly-reduced residual for the surface/double models: the helix
    //    model contributes fh/4 to C11 and C33, −fh/4 to Re C13.
    double r11 = cov.c11 - fv - fh / 4.0;
    double r33 = cov.c33 - fv - fh / 4.0;
    const std::complex<double> r13 = cov.c13 - fv / 3.0 + fh / 4.0;

    if ( r11 < 0.0 )
        r11 = 0.0;
    if ( r33 < 0.0 )
        r33 = 0.0;

    const SurfaceDoubleSplit split = solveSurfaceDouble( r11, r33, r13 );

    out->ok = true;
    out->surface = split.ps;
    out->doubleBounce = split.pd;
    out->volume = ( 8.0 / 3.0 ) * fv;
    out->helix = std::abs( fh ) * ( 3.0 / 4.0 );
    return true;
}

} // namespace sicnu::sar
