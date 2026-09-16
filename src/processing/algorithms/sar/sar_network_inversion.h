// sar_network_inversion.h — small-baseline linear network inversion
// (Advanced InSAR 11.0, package F; DECISIONS D-006).
//
// WHAT THIS IS: the classic SBAS-style LINEAR inversion — given per-pair
// LOS displacement samples d_i = u_{m(i)} − u_{s(i)} (metres; epoch
// displacements u relative to the reference epoch) of a pair network,
// solve the weighted least-squares system
//   min Σ_i w_i·(d_i − (G·u)_i)²
// per pixel, where G is the pair graph's incidence matrix and w the
// pair-level weight vector. From the solved epoch displacements it
// derives a linear velocity (OLS slope over the solved epochs) and the
// fit RMS residual.
//
// WEIGHT CONTRACT (D-006, precise): w is a PER-PAIR vector — a property
// of the STACK (e.g. each pair's mean coherence²), constant across
// pixels, folded into the normal equations at pattern-factorization
// time. A per-pixel SCALAR quality weight is mathematically inert (a
// scalar cancels out of WLS) and is therefore NOT an input; per-pixel
// per-pair weight variation is not supported and must not be smuggled
// in. Uniform weights = omit pairWeights.
//
// MISSING DATA (per pixel): a NaN displacement drops its ROW from that
// pixel's system. Epoch connectivity is decided PER PIXEL from the
// surviving rows (union-find over epochs): only the reference epoch's
// component is solved; other components' epochs stay NaN and their rows
// are dropped (counted in droppedPairs). maskStrategy=intersect (drop
// the whole pixel when ANY pair is NaN) is an OPERATOR-seam policy
// layered on top; the kernel itself always solves per-pixel.
//
// SOLVE COST MODEL (bounded; PERFORMANCE.md): epochs ≤ 200, pairs ≤ 64
// (the u64 pattern mask bound — larger stacks must run behind a fully
// valid intersect mask in the operator seam), NaN patterns cacheable ≤
// maxPatterns (default 128) — each distinct validity pattern is
// union-found + factorized ONCE (dense Cholesky of GᵀWG, O(epochs³)) and
// reused for every pixel sharing the pattern. Beyond the bound: typed
// refusal NETWORK_INVERSION_PATTERN_BLOWUP (the operator then advises the
// intersect strategy; nothing degrades silently). A rank-deficient system
// (degenerate weights or a broken sub-network) is a typed refusal, never
// a pseudo-inverse.
//
// DETERMINISM: fixed iteration order, no RNG, no threading; identical
// inputs give bit-identical outputs.
#pragma once

#include <QString>

#include <map>
#include <vector>

namespace sicnu::sar
{

struct NetworkInversionProblem
{
    int epochCount = 0;   ///< distinct acquisition epochs (reference = 0)
    int pairCount = 0;
    std::vector<int> pairMasterEpoch;      ///< per pair, master epoch index
    std::vector<int> pairSlaveEpoch;       ///< per pair, slave epoch index
    std::vector<double> pairTemporalYears; ///< per pair, |t_m − t_s|/365.25 (QA only)
    /// Optional per-pair weights (> 0, finite; e.g. mean coherence²).
    /// Empty = uniform. Constant across the stack (see the weight contract).
    std::vector<double> pairWeights;
    /// Contract validation: counts consistent, 0 ≤ slave < master <
    /// epochCount (masters precede slaves), finite temporals, weights
    /// positive when present.
    bool isValid() const;
};

class NetworkInversionSolver
{
  public:
    /// @param maxPatterns distinct NaN-pattern cache bound (typed refusal
    /// beyond — NETWORK_INVERSION_PATTERN_BLOWUP; the intersect strategy
    /// is the operator seam's conscious alternative).
    explicit NetworkInversionSolver( const NetworkInversionProblem &problem,
                                     int maxPatterns = 128 );

    /// One pixel's solve. @p displacement: pairCount entries (NaN =
    /// missing row). Outputs: @p epochDisplacement (epochCount entries,
    /// caller-allocated; [0] = 0; NaN outside the reference component),
    /// @p velocityMPerYear (OLS slope against @p epochTemporalYears —
    /// epochCount ascending years since the reference; null = uniform
    /// one-year spacing, the documented fallback), @p rmsResidualM over
    /// the kept rows, @p solvedEpochs, @p droppedPairs (missing rows plus
    /// rows outside the reference component).
    ///
    /// @return false ONLY on a typed permanent refusal (pattern blowup /
    /// rank-deficient system) carried in @a error; per-pixel missing data
    /// never fails the call (it degrades to NaN epochs + dropped counts).
    bool solvePixel( const double *displacement, const double *epochTemporalYears,
                     double *epochDisplacement, double *velocityMPerYear,
                     double *rmsResidualM, int *solvedEpochs, int *droppedPairs,
                     QString *error = nullptr );

    long distinctPatterns() const { return static_cast<long>( m_patterns.size() ); }
    int maxPatterns() const { return m_maxPatterns; }

  private:
    struct PatternSolution
    {
        std::vector<int> keptPairs;    ///< reference-component rows (ascending)
        std::vector<int> solvedEpochs; ///< ascending epoch indices (reference excluded)
        std::vector<double> chol;      ///< L·Lᵀ = GᵀWG, row-major lower triangle (dim²)
        size_t dim = 0;                ///< solvedEpochs.size()
    };
    const PatternSolution &patternFor( uint64_t mask, QString *error, bool *ok );

    NetworkInversionProblem m_problem;
    int m_maxPatterns;
    std::map<uint64_t, PatternSolution> m_patterns;
};

} // namespace sicnu::sar
