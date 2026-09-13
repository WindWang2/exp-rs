// sar_polsar.h — full-polarimetric (PolSAR) decomposition kernels
// (Advanced SAR / PolSAR / InSAR 10.0, package B).
//
// MODE CONTRACT (honesty rule, extends sar-domain.md §1): these kernels
// consume FULL-POL complex channels (SHH, SHV reciprocal, SVV). Dual-pol
// VV/VH detected inputs are NOT a valid substitute — the operator seam
// refuses them (POLARIZATION_MISMATCH); no dual-pol approximation is
// relabeled as a quad-pol decomposition.
//
// Channel model: monostatic backscatter with reciprocity (SHV = SVH — the
// 3-channel contract "HH;HV;VV"; see sar_complex.h). Scattering vector in
// the lexicographic basis u = [SHH, SHV, SVV]^T and the Pauli basis
// k = [ (SHH+SVV)/√2, (SHH−SVV)/√2, √2·SHV ]^T (unitary pair; k^H·k =
// |SHH|² + 2|SHV|² + |SVV|² = SPAN, counting both cross-pol channels).
//
// Ensemble contract: single-look per-pixel covariances are rank 1 (H ≡ 0,
// A undefined). Every model-based/eigen decomposition in this file consumes
// an ENSEMBLE covariance accumulated over a spatial window
// (`PolEnsemble3` → `finalizeEnsemble`); the window size is the operator's
// business, and the effective-resolution cost is documented at the seam.
// All accumulation is in double precision over valid samples only
// (invalid = NaN components, per sar_complex.h normalization).
//
// MODEL CONVENTIONS (the definitions below are the contract; the
// known-answer tests pin exactly these matrices, docs/processing/
// sar-domain.md §7 restates them):
//   Volume (random dipoles, Freeman-Durden 1998):
//       Cv = fv · [[1, 0, 1/3], [0, 2/3, 0], [1/3, 0, 1]]   → fv = 1.5·C22
//   Double bounce:  Cd = fd · [[|α|², 0, α], [0,0,0], [conj α, 0, 1]]
//   Surface:        Cs = fs · [[1, 0, conj β], [0,0,0], [β, 0, |β|²]]
//   Helix: the covariance of the canonical circular point target,
//       u_helix = [1, j, −1]/2 (right helicity; left = conjugate, i.e.
//       fh < 0): Ch = fh · u·u^H — purely imaginary C12 = C23 = −j·fh/4,
//       real C13 = −fh/4, diagonal fh/4 each, trace fh·(3/4).
//   Freeman-Durden branch rule: Re(C13 − fv/3) ≥ 0 → surface branch
//   (β solved, α = 0); < 0 → double branch (α solved, β = 0). Negative
//   residual powers are clamped to 0 (recorded by the caller-visible power
//   values; the clamp breaks exact SPAN conservation and is documented).
//   Yamaguchi 4-component: helix first (fh = −4·mean(Im C12, Im C23)),
//   then volume from the helix-decontaminated cross-pol power
//   (fv = 1.5·(C22 − fh/4)), then surface/double via the same branch rule
//   on the doubly-reduced residual. The orientation-angle-adapted volume
//   model of the refined (2012) Yamaguchi variant is NOT implemented —
//   documented honest scope.
#pragma once

#include <complex>

namespace sicnu::sar
{

/// Ensemble accumulator for the reciprocal covariance matrix
/// C = E[u·u^H], u = [SHH, SHV, SVV]. Accumulate valid samples only.
struct PolEnsemble3
{
    long samples = 0;
    double c11 = 0.0; // E|SHH|²
    double c22 = 0.0; // E|SHV|²
    double c33 = 0.0; // E|SVV|²
    std::complex<double> c12 = {}; // E[SHH·conj(SHV)]
    std::complex<double> c13 = {}; // E[SHH·conj(SVV)]
    std::complex<double> c23 = {}; // E[SHV·conj(SVV)]
};

/// Accumulates one sample; NaN-component samples are ignored (counted as
/// invalid — they never enter the averages).
void accumulatePolSample( PolEnsemble3 &ens, std::complex<double> shh,
                          std::complex<double> shv, std::complex<double> svv );

/// Normalizes the accumulated sums into a covariance (divides by the valid
/// sample count). @return false when no valid sample was accumulated
/// (callers emit NaN products for the pixel).
bool finalizeEnsemble( const PolEnsemble3 &ens, PolEnsemble3 *covariance );

/// Pauli single-look powers (no ensemble): odd = |SHH+SVV|²/2 (sphere/
/// odd-bounce), double = |SHH−SVV|²/2 (dihedral), volume = 2|SHV|²,
/// span = sum of the three. NaN propagates from invalid samples.
struct PauliPowers
{
    double odd = 0.0;
    double doubleBounce = 0.0;
    double volume = 0.0;
    double span = 0.0;
};
PauliPowers pauliPowers( std::complex<double> shh, std::complex<double> shv,
                         std::complex<double> svv );

/// Cloude-Pottier H/A/α eigen-decomposition products of the coherency
/// matrix T3 (T3 = unitary transform of the covariance; see
/// coherencyFromCovariance). NaN semantics: entropy/alpha are NaN when the
/// total power is 0; anisotropy is (λ2−λ3)/(λ2+λ3) — NaN for rank-1
/// ensembles (λ2 = λ3 = 0), which is the honest answer, not a zero.
struct HAlphaResult
{
    double entropy = 0.0;    ///< H ∈ [0, 1] (log base 3)
    double anisotropy = 0.0; ///< A ∈ [0, 1] (NaN for rank-1)
    double alphaMean = 0.0;  ///< mean α in degrees [0, 90]
    double lambda[3] = { 0.0, 0.0, 0.0 }; ///< descending eigenvalues
    double dominance = 0.0;  ///< λ1 / (λ1+λ2+λ3) — eigen stability
};
bool cloudePottier( const PolEnsemble3 &cov, HAlphaResult *out );

/// Freeman-Durden three-component powers (linear, trace-3 convention:
/// Ps + Pd + Pv equals the covariance trace up to documented clamping).
struct FreemanDurdenResult
{
    double surface = 0.0;     ///< Ps
    double doubleBounce = 0.0; ///< Pd
    double volume = 0.0;      ///< Pv = (8/3)·fv
    bool ok = false;
};
bool freemanDurden( const PolEnsemble3 &cov, FreemanDurdenResult *out );

/// Yamaguchi four-component powers (surface/double/volume/helix).
struct YamaguchiResult
{
    double surface = 0.0;
    double doubleBounce = 0.0;
    double volume = 0.0;
    double helix = 0.0; ///< ≥ 0; |helicity| reported, sign folded
    bool ok = false;
};
bool yamaguchi4( const PolEnsemble3 &cov, YamaguchiResult *out );

/// Covariance → coherency (Pauli basis) conversion:
///   T11 = (C11 + C33 + 2·Re C13)/2      T22 = (C11 + C33 − 2·Re C13)/2
///   T33 = 2·C22                          T12 = (C11 − C33 − 2j·Im C13)/2
///   T13 = C12 + conj(C23)                T23 = C12 − conj(C23)
/// (k = [(SHH+SVV)/√2, (SHH−SVV)/√2, √2·SHV] carries the 4-port SPAN:
/// trace(T) = C11 + 2·C22 + C33. T is therefore NOT unitarily similar to
/// the 3×3 C — its eigenvalues equal those of the reciprocal 4×4
/// covariance, which is the ensemble Cloude-Pottier consumes.)
struct Coherency3
{
    double t11 = 0.0, t22 = 0.0, t33 = 0.0;
    std::complex<double> t12 = {}, t13 = {}, t23 = {};
};
Coherency3 coherencyFromCovariance( const PolEnsemble3 &cov );

} // namespace sicnu::sar
