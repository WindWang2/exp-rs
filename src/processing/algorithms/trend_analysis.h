/***************************************************************************
  processing/algorithms/trend_analysis.h
  Temporal Phenology Timeline Studio (D16) — Theil-Sen / Mann-Kendall.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Non-parametric monotonic trend for one series or a whole raster stack.
  Definitions (Gilbert 1987 ch. 16; ties corrected):

    S       = Σ_{i<j, t_i<t_j} sgn(y_j − y_i)
    var(S)  = [n(n−1)(2n+5) − Σ_p t_p(t_p−1)(2t_p+5)] / 18
    z       = (S ∓ 1)/√var(S)   (±1 continuity correction; 0 when S = 0)
    p       = erfc(|z|/√2)      (two-sided standard-normal tail)
    slope   = median of pairwise (y_j − y_i)/(t_j − t_i) over t_i < t_j
    tau     = S / (n(n−1)/2)    (Kendall's tau-a; the tie-adjusted tau-b is
                                  not reported — S and var(S) are)

  NaN samples are absent. Fewer than 3 finite observations (or no strictly
  time-ordered pair) yields NaN outputs with the true sampleCount — an
  underpowered test is reported, never guessed. Slopes are per day on the
  caller's @a tDays axis. Deterministic, single-threaded, O(n²) per series.
 ***************************************************************************/

#ifndef SICNU_PROCESSING_ALGORITHMS_TREND_ANALYSIS_H
#define SICNU_PROCESSING_ALGORITHMS_TREND_ANALYSIS_H

#include <vector>

namespace sicnu::temporal
{

struct MannKendallResult
{
    double senSlope = 0.0;    ///< Theil-Sen median pairwise slope (units/day)
    double intercept = 0.0;   ///< median of (y_i − slope·t_i)
    double tau = 0.0;         ///< Kendall's tau-a in [−1, 1]
    double zScore = 0.0;      ///< continuity-corrected standardized statistic
    double pValue = 1.0;      ///< two-sided significance
    double tauVariance = 0.0; ///< tie-corrected var(S)
    int sampleCount = 0;      ///< finite observations used
    bool valid = false;       ///< false when the test is undefined

    bool isSignificant( double alpha = 0.05 ) const { return valid && pValue < alpha; }
};

class TrendAnalyzer
{
  public:
    /// Single-series Theil-Sen slope + Mann-Kendall test.
    static MannKendallResult computeMannKendall( const std::vector<float> &y,
                                                 const std::vector<double> &tDays );

    /// Raster batch over a time-major stack inSeries[t·height·width +
    /// row·width + col]. Writes per-pixel slope (units/day), two-sided
    /// p-value and z-score maps; pixels with an undefined test get NaN in
    /// all three outputs. NaN sample handling identical to the single
    /// series entry point.
    static void computeRasterTrend( const float *inSeries, int width, int height,
                                    int timeSteps, const double *tDays, float *outSlope,
                                    float *outPValue, float *outZScore );
};

} // namespace sicnu::temporal

#endif // SICNU_PROCESSING_ALGORITHMS_TREND_ANALYSIS_H
