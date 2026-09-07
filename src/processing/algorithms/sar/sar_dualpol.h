// sar_dualpol.h — dual-polarization feature kernels (Foundation 5.0, D.2).
//
// Numeric domain: linear POWER values (the sar_metadata.h contract). Operators
// resolve declared SICNU_SAR_DOMAIN (linear_power|db) or accept an explicit
// domain parameter and convert to linear before these kernels; nonpositive
// values are outside the linear-power domain and yield NaN (per-kernel
// NoData policy: never silently clamped, never a plausible 0 product).
//
// Features (per pixel, VV and VH co-polarized/cross-polarized linear power):
//   ratio                = VV / VH
//   normalized_difference = (VV − VH) / (VV + VH)      ∈ [−1, 1]
//   log_ratio            = 10·log10(VV / VH)           (dB)
//   rvi                  = 4·VV / (VV + VH)
//       Dual-pol Radar Vegetation Index — the common Sentinel-1 dual-pol
//       APPROXIMATION. Not the quad-pol RVI (4σ/(σVV+σVH+2σHV)), which needs
//       a cross-pol channel this platform does not model; docs must not
//       claim quad-pol capability.
//   span                 = VV + VH  (dual-pol total-power approximation)
// Zero denominators (VV+VH == 0) are NaN; VH == 0 makes ratio/log_ratio NaN.
#pragma once

namespace sicnu::sar
{

enum class DualPolFeature
{
    Ratio,
    NormalizedDifference,
    LogRatio,
    Rvi,
    Span,
};

const char *dualPolFeatureToString( DualPolFeature feature );
/// @return false (leaves @a out untouched) on an unknown token.
bool parseDualPolFeature( const char *token, DualPolFeature *out );

/// Computes one feature from linear-power VV/VH; NaN on domain violations.
double dualPolFeature( DualPolFeature feature, double vv, double vh );

} // namespace sicnu::sar
