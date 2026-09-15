// src/processing/algorithms/radiometric_qa.h — per-pixel radiometric QA
// flag vocabulary and calibration-chain propagation (radiometric-physics-11,
// work package F).
//
// QaMask already derives *binary masks* from QA bands (Landsat QA_PIXEL, S2
// SCL). What the platform lacked is the *radiometric* side of QA: a stable
// per-pixel flag vocabulary for what calibration/normalization does to each
// pixel (saturation, negative / over-range reflectance, non-finite values),
// plus propagation so flags raised by one step survive through the chain
// into the delivered product and its summary. Both feed the GOAL Oracle
// requirement that every output pixel's state is traceable.
//
// Flag-word contract:
//   * Flags are a public uint16_t bit vocabulary (values documented here and
//     frozen by tests); a flag word of 0 means a clean, finite, in-domain
//     pixel.
//   * Producers OR their bits in; propagate() unions words across steps —
//     anomalies never clear themselves downstream.
//   * Summary counts are per-flag pixel counts over the buffer (a pixel can
//     contribute to several flags), plus the valid-pixel total, so an
//     operator result can state "0.4% saturated, 2.1% cloud-shadowed" with
//     denominators that are never re-derived from the raster.
#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>

namespace RadiometricQa
{

/// Per-pixel flag bits (frozen vocabulary — tests pin every value).
enum Flag : uint16_t
{
    FlagNone = 0,
    FlagSaturated = 1u << 0,      ///< value at/above the sensor saturation level
    FlagNegative = 1u << 1,       ///< reflectance < 0 (unphysical post-correction)
    FlagOverRange = 1u << 2,      ///< reflectance > 1 (unit-domain violation)
    FlagNotFinite = 1u << 3,      ///< NaN / ±Inf anywhere in the chain
    FlagCloud = 1u << 4,          ///< propagated from QA band / SCL
    FlagCloudShadow = 1u << 5,    ///< propagated from QA band / SCL
    FlagSnow = 1u << 6,           ///< propagated from QA band / SCL
    FlagSaturationQa = 1u << 7,   ///< sensor QA_RADSAT-style saturation bit
    FlagInvalidAngles = 1u << 8,  ///< geometry unusable (sun below horizon, zenith ≥ 90°)
};

/// Accumulated per-flag counts for one buffer evaluation.
struct Summary
{
    size_t pixels = 0;        ///< total pixels evaluated
    size_t counts[9] = {};    ///< indexed by bit position (bit i → counts[i])
    size_t flaggedPixels = 0; ///< pixels with any flag set

    /// Fraction of @p pixels carrying flag @p f (0 when pixels == 0).
    double fraction( uint16_t flag ) const;
};

/**
 * Evaluates one reflectance band into per-pixel flag words.
 *
 * @param rho             reflectance buffer (NaN/Inf handled, never aborted on)
 * @param flags           output buffer, same length (FlagNone = clean)
 * @param saturationLevel values >= this are FlagSaturated; pass ≤ 0 to skip
 *                        threshold saturation (sensor QA saturation still lands
 *                        via addSaturationBits)
 * @param summary         optional accumulation target (may be null)
 */
void evaluateReflectance( const float *rho, uint16_t *flags, size_t count,
                          float saturationLevel, Summary *summary = nullptr );

/// Unions @p stepFlags into @p flags element-wise (chain propagation: the
/// buffers may alias, in which case the call is a no-op).
void propagate( uint16_t *flags, const uint16_t *stepFlags, size_t count );

/// Marks @p which on every pixel where @p mask (1 = obscured, 0 = clear —
/// the QaMask convention) is nonzero.
void markFromMask( uint16_t *flags, const uint8_t *mask, size_t count, uint16_t which );

/// Sets FlagSaturationQa where (@p qaBits & @p bandMask) != 0 (Landsat
/// QA_RADSAT convention: one bit per band inside the QA word).
void addSaturationBits( uint16_t *flags, const uint16_t *qaBits, size_t count,
                        uint16_t bandMask );

/**
 * Linear uncertainty propagation for one calibration step
 *   y = gain·x + bias
 * with per-pixel input uncertainty @p sigmaX and constant gain/bias
 * uncertainties:
 *   σy² = (gain·σx)² + (x·σ_gain)² + σ_bias²
 * NaN pixels propagate as NaN. Gain/bias sigmas may be 0 (default).
 */
bool propagateLinearUncertainty( const float *x, const float *sigmaX, size_t count,
                                 double gain, double gainSigma, double biasSigma,
                                 float *sigmaY, QString *errorMessage = nullptr );

} // namespace RadiometricQa
