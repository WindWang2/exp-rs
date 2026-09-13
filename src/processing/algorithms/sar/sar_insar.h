// sar_insar.h — InSAR base-chain kernels (Advanced SAR / PolSAR / InSAR
// 10.0, package C).
//
// SCOPE (honest subset): this module implements the base interferometric
// chain on CO-REGISTERED complex SLC pairs — pair preflight inputs, the
// interferogram/coherence product, Goldstein-Werner phase filtering, a
// reference quality-guided phase unwrapper, flat-earth ramp removal as a
// robust polynomial fit, and phase-to-displacement conversion. It does NOT
// implement: full image co-registration (only a global-translation
// refinement — see coregistrationShift below), topographic phase removal
// from DEM/orbit, atmospheric correction, or PSI/SBAS time-series
// analysis. Claims beyond this list are defects, not features.
//
// Numeric domain: wrapped phase is radians in (−π, π]; coherence ∈ [0, 1];
// displacement in metres along the line of sight (sign convention:
// d_los = −λ·φ_unwrapped/(4π), i.e. positive d_los = motion TOWARD the
// sensor). Invalid samples are NaN — never clamped.
//
// Memory contract: window kernels (coherence, Goldstein) are tile-safe.
// fitPhaseRamp/qualityGuidedUnwrap consume FULL-PLANE buffers — the
// operator seam must gate them through the shared resource estimation and
// refuse with a typed error when the plane exceeds the budget (the
// reference unwrapper is deliberately single-scale; tiling breaks global
// phase connectivity and is NOT approximated).
//
// Unwrapping (DECISIONS D-001): qualityGuidedUnwrap is the BUILT-IN
// reference implementation — deterministic (quality-descending with
// (row, col) tie-break), exact when the Itoh condition (|Δφ| < π) holds
// between neighbors, and it seeds from the highest-quality valid pixel.
// Residue-dense fields are NOT globally optimized (no branch cuts / MCF /
// SNAPHU semantics) — the operator exposes an explicit `provider`
// parameter: any value other than "builtin" is a typed refusal
// (UNWRAP_PROVIDER_UNAVAILABLE) so external tools can slot in without the
// built-in silently pretending to be them.
#pragma once

#include <cstdint>
#include <complex>
#include <vector>

namespace sicnu::sar
{

/// Coherence of the master/slave ensemble around one pixel:
///   |Σ s1·conj(s2)| / sqrt(Σ|s1|² · Σ|s2|²)
/// over the (2r+1)² window centered at (cx, cy) inside the halo buffers
/// (row-major, width @a w; buffers are the native CFloat32 stream layout).
/// Accumulation is double precision. NaN when either denominator is 0 or
/// the window holds no jointly valid pair (NaN pairs are skipped).
double windowCoherence( const std::complex<float> *s1, const std::complex<float> *s2,
                        int w, int h, int cx, int cy, int radius );

/// Interferometric phase of one sample pair: arg(s1·conj(s2)) ∈ (−π, π].
/// NaN for invalid (non-finite or exactly zero) operands. Accepts the
/// native float-complex stream samples (implicit widening).
double interferogramPhase( std::complex<double> s1, std::complex<double> s2 );

/// One complex interferogram sample: s1·conj(s2) (NaN components propagate
/// through the NaN normalization of the callers).
std::complex<double> interferogramSample( std::complex<double> s1, std::complex<double> s2 );

/// Goldstein-Werner spectral interferogram filter, spatial form, evaluated
/// at one pixel: Z_f = Σ_w |z|^α·z / Σ_w |z|^α over the (2r+1)² window of
/// the complex interferogram. α = 1 reduces to magnitude-weighted averaging,
/// α = 0 to plain phasor averaging. Returns the filtered PHASOR (unit
/// magnitude) — the filter is defined on phase. NaN when the window has no
/// valid sample.
double goldsteinPhase( const std::complex<float> *ifg, int w, int h,
                       int cx, int cy, int radius, double alpha );

/// Robust polynomial ramp fit (flat-earth approximation). Streaming
/// accumulator form: samples are added in visit order (deterministic), the
/// normal equations accumulate in O(1) memory, and the robust IQR clipping
/// uses a bounded residual reservoir (≤ 65536 stride-sampled residuals) so
/// full-raster fits never materialize a phase plane.
/// fit() runs `robustIterations` refits (3 recommended); @return false when
/// fewer valid samples than model coefficients were added or the normal
/// matrix is singular.
struct PhaseRampModel
{
    bool quadratic = false;
    double coef[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
};

class PhaseRampFitter
{
  public:
    /// Non-finite phases are ignored (valid-sample bookkeeping is internal).
    void addSample( int x, int y, double phase );

    /// @param robustIterations 0 = plain least squares.
    bool fit( bool quadratic, int robustIterations, PhaseRampModel *out );

    long sampleCount() const { return m_samples; }

  private:
    long m_samples = 0;
    // Sums for the linear part (always accumulated): Σ1, Σx, Σy, Σx², Σxy,
    // Σy², Σφ, Σxφ, Σyφ. The quadratic extension reuses the generic
    // normal-matrix accumulation below.
    struct SampleReservoir
    {
        static constexpr size_t kMax = 65536;
        std::vector<double> phase;
        std::vector<std::pair<int, int>> xy;
        long seen = 0;
        void add( int x, int y, double phi )
        {
            ++seen;
            if ( phase.size() < kMax )
            {
                phase.push_back( phi );
                xy.emplace_back( x, y );
            }
        }
    };
    SampleReservoir m_reservoir;
};

/// Convenience plane form over the streaming fitter (tests and small
/// diagnostics): iterates the plane in row-major order. Finite entries only.
bool fitPhaseRamp( const double *phase, int w, int h, bool quadratic, PhaseRampModel *out );

/// Evaluates the ramp at (x, y) — exposed for tests and operator diagnostics.
double evalPhaseRamp( const PhaseRampModel &m, int x, int y );

/// Reference quality-guided unwrapper (see file header honesty note).
/// Seeds at the highest-quality valid pixel of every connected valid
/// component (NaN holes / barriers split the field; cross-component
/// relative offsets are undefined by construction), then floods outward
/// through valid neighbors in quality-descending order (ties broken by
/// row, then column, then insertion sequence): each pixel is unwrapped
/// relative to the neighbor that discovered it via φ + 2π·round((φ_ref −
/// φ)/2π). Pixels in no valid component stay NaN.
/// @param quality may be null → uniform quality (row/column order unwrap).
struct UnwrapResult
{
    std::vector<double> unwrapped;      ///< w*h, NaN where unreached/invalid
    long unwrappedCount = 0;            ///< pixels with a finite result
    long seeds = 0;                     ///< connected components seeded
};
bool qualityGuidedUnwrap( const double *wrapped, const double *quality,
                          int w, int h, UnwrapResult *out );

/// Line-of-sight displacement from unwrapped phase:
/// d = −λ·φ/(4π). @p wavelengthM must be > 0 (callers refuse otherwise).
double losDisplacementM( double unwrappedPhaseRad, double wavelengthM );

/// Itoh discontinuity ratio: the fraction of horizontal/vertical neighbor
/// pairs of the (unwrapped) phase whose jump exceeds π — the honest
/// "this field still looks wrapped" diagnostic (DECISIONS D-009).
/// NaN when no valid neighbor pair exists.
double phaseDiscontinuityRatio( const double *phase, int w, int h );

/// Global complex shift estimate between co-registered-ish SLC magnitude
/// fields: normalized cross-correlation of |s| on a patch lattice
/// (patchSize × patchSize every @a patchStride pixels), integer peak per
/// patch within ±searchRadius, parabolic sub-pixel refinement, MEDIAN over
/// patches (robust to decorrelated patches). Patches without a confident
/// peak (ratio < @a minPeakRatio between best and second-best distinct
/// shift) are dropped. @return false when fewer than 3 confident patches
/// remain (callers refuse instead of guessing a shift).
struct CoregisterShift
{
    double dx = 0.0; ///< slave shift in pixels (x)
    double dy = 0.0; ///< slave shift in pixels (y)
    long confidentPatches = 0;
    double meanPeakRatio = 0.0;
};
bool coregistrationShift( const std::complex<float> *master, const std::complex<float> *slave,
                          int w, int h, int searchRadius, int patchSize, int patchStride,
                          double minPeakRatio, CoregisterShift *out );

/// Bilinear complex resample of @a src by (dx, dy) into @a dst (both w*h):
/// dst(x, y) = src(x − dx, y − dy) with out-of-range → NaN. Deterministic.
void shiftComplexBilinear( const std::complex<float> *src, int w, int h,
                           double dx, double dy, std::complex<float> *dst );

} // namespace sicnu::sar
