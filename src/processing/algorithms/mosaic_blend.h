// mosaic_blend.h — F15 Package D: seam blending for quality mosaics
// (ADR 0163).
//
// Feather blending cross-fades across the seam with a linear ramp of width
// `featherWidth`; the two sides' weights always sum to 1 and per-pixel NoData
// falls back to the other side, so a blended mosaic cannot grow NoData cracks
// or double-contributed boundaries along the seam.
//
// Multiband blending (windowed Laplacian pyramid) softens low-frequency
// radiance steps across the seam while keeping high-frequency detail. It is
// applied per seam-band window only, so memory stays bounded by the window,
// not the mosaic. With constant inputs every pyramid level is constant and
// the reconstruction reproduces s·value exactly — the known-answer oracle.
#pragma once

#include <vector>

namespace rs::mosaic {

enum class BlendMode { None, Feather, Multiband };

struct BlendOptions {
    BlendMode mode = BlendMode::Feather;
    int featherWidth = 32;   // pixels; ramp half-width across the seam
    int multibandLevels = 4; // pyramid depth for Multiband (>= 1)
};

/// Feather weight for a pixel at signed distance `d` (pixels, positive on
/// the incoming-scene side): 0.5 at the seam, clamped linear ramp to 0/1.
double featherWeight( double signedDistancePx, int featherWidth );

/// Blend two pixel values with weight s (weight of b), NoData-aware:
/// exactly one side NaN -> the other side; both NaN -> NaN.
float blendValue( float a, float b, double s );

/// Laplacian-pyramid blend of two equal-size windows with a per-pixel weight
/// map wB (weight of b, in [0,1]). `levels >= 1`; level 0 is the window
/// itself. Convex per level, so the result stays within [min(a,b), max(a,b)]
/// pixel-wise and reproduces s·value exactly for constant inputs.
/// Returns false on mismatched sizes / invalid levels.
bool blendMultibandWindow( const std::vector<float> &a, const std::vector<float> &b,
                           const std::vector<double> &wB, int width, int height, int levels,
                           std::vector<float> *out );

} // namespace rs::mosaic
