/***************************************************************************
  processing/algorithms/phenology_metrics.h
  Temporal Phenology Timeline Studio (D16) — phenology parameter extraction.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Smoothed-season phenology for one regular-calendar series (NaN = missing).
  All returned day values are on the caller's @a tDays axis (day offsets from
  the series epoch); a day-of-year view is derived internally as
  doy(t) = floor(fmod(t, 365.25)) + 1 ∈ [1, 366] (epoch-aligned seasons;
  mean Gregorian year to limit leap-year drift vs a fixed-365 modulo).

  Biology guard (ADR 0161): a metric set is valid only when
  sos < pos < eos on the (possibly year-wrapping) axis and
  los = eos − sos ≥ 0. Reversed or degenerate seasons are reported with
  valid = false, never silently re-ordered.

  Threshold semantics (D16 §C): with the season's range normalized
  Ratio(t) = (z − z_min)/(z_max − z_min), SOS is the first rising crossing of
  thresholdFraction (linear interpolation between samples), POS the argmax,
  EOS the first falling crossing after the peak. Metrics live on the caller's
  absolute tDays axis, where sos < eos is enforced, so los = eos − sos; the
  familiar +365 wrap belongs to the day-of-year VIEW, not to this axis.
 ***************************************************************************/

#ifndef SICNU_PROCESSING_ALGORITHMS_PHENOLOGY_METRICS_H
#define SICNU_PROCESSING_ALGORITHMS_PHENOLOGY_METRICS_H

#include <utility>
#include <vector>

namespace sicnu::temporal
{

/// Fitted asymmetric double-logistic season
/// f(t) = base + amp·( σ(k1·(t−τ1)) − σ(k2·(t−τ2)) ),
/// with σ(x) = 1/(1+exp(−x)); τ1 is the greenup inflection (rising curvature
/// extreme), τ2 the senescence inflection (falling curvature extreme).
struct DoubleLogisticParams
{
    double baseVal = 0.0;       ///< winter/base background level
    double amplitude = 0.0;     ///< seasonal amplitude (max − base)
    double sosInflection = 0.0; ///< greenup inflection τ1 (tDays axis)
    double sosRate = 0.0;       ///< rising rate k1 (1/day)
    double eosInflection = 0.0; ///< senescence inflection τ2 (tDays axis)
    double eosRate = 0.0;       ///< senescence rate k2 (1/day)
};

struct PhenologyMetrics
{
    double sos = -1.0;     ///< start of season (tDays axis); −1 when undefined
    double pos = -1.0;     ///< peak of season
    double eos = -1.0;     ///< end of season
    double los = 0.0;      ///< length of season (days; wrapped seasons +365)
    double baseVal = 0.0;  ///< seasonal base (z_min inside the window)
    double peakVal = 0.0;  ///< seasonal peak (z_max inside the window)
    double integral = 0.0; ///< Σ value·Δday trapezoid over [sos, eos]
    bool valid = false;    ///< biology guard passed (sos < pos < eos)
};

class PhenologyExtractor
{
  public:
    /// Dynamic relative-threshold extraction on one season window
    /// [seasonStartDoy, seasonEndDoy] (day-of-year, inclusive; start > end
    /// wraps the year). Fewer than 3 finite samples, a flat window, or a
    /// missing rising/falling limb yields valid = false.
    static PhenologyMetrics extractDynamicThreshold( const std::vector<float> &y,
                                                     const std::vector<double> &tDays,
                                                     double thresholdFraction = 0.2,
                                                     int seasonStartDoy = 1,
                                                     int seasonEndDoy = 365 );

    /// Nonlinear least-squares fit (Levenberg–Marquardt, numeric Jacobian) of
    /// the asymmetric double logistic; phenology from the logistic
    /// inflections (maximum-slope points): sos = τ1, eos = τ2, pos = argmax
    /// of the fitted curve. The pair's metrics share the validity guard above.
    static std::pair<DoubleLogisticParams, PhenologyMetrics>
    fitDoubleLogistic( const std::vector<float> &y, const std::vector<double> &tDays );

    /// Multi-cycle segmentation (double-crop paddy, double cropping): the
    /// series' derivative zero structure is reduced to peak/valley pairing;
    /// @a cycles most prominent peaks define that many seasons, split at the
    /// deepest valleys between them. Each season is scored with the dynamic
    /// threshold semantics. Fewer distinct peaks than @a cycles → fewer
    /// results (never padded, never merged).
    static std::vector<PhenologyMetrics> extractMultiCycle( const std::vector<float> &y,
                                                            const std::vector<double> &tDays,
                                                            int cycles = 2,
                                                            double thresholdFraction = 0.2 );
};

} // namespace sicnu::temporal

#endif // SICNU_PROCESSING_ALGORITHMS_PHENOLOGY_METRICS_H
