// src/processing/algorithms/spectral_indices.h
#pragma once

#include <cstddef>

/**
 * Spectral index calculation functions.
 *
 * All functions operate on raw float arrays (band data).
 * Output arrays must be pre-allocated with the same size as input.
 * Returns true on success, false on invalid arguments.
 *
 * Convention: NaN is used for undefined values (e.g., 0/0).
 */
namespace SpectralIndices
{
    /**
     * NDVI = (NIR - Red) / (NIR + Red)
     * @param nir   NIR band data
     * @param red   Red band data
     * @param out   output buffer
     * @param count number of pixels
     */
    bool ndvi(const float *nir, const float *red, float *out, size_t count);

    /**
     * EVI = 2.5 * (NIR - Red) / (NIR + 6*Red - 7.5*Blue + 1)
     */
    bool evi(const float *nir, const float *red, const float *blue, float *out, size_t count);

    /**
     * SAVI = (NIR - Red) / (NIR + Red + L) * (1 + L), where L=0.5
     */
    bool savi(const float *nir, const float *red, float *out, size_t count);

    /**
     * NDWI = (Green - NIR) / (Green + NIR)
     */
    bool ndwi(const float *green, const float *nir, float *out, size_t count);

    /**
     * NDBI = (SWIR - NIR) / (SWIR + NIR)
     */
    bool ndbi(const float *swir, const float *nir, float *out, size_t count);

    /**
     * MNDWI = (Green - SWIR) / (Green + SWIR)
     */
    bool mndwi(const float *green, const float *swir, float *out, size_t count);

    // --- Foundation 5.0 (Milestone B2) index families -----------------------
    // Scale note: ratio indices (GNDVI/NDMI/ARVI/UI/BUI) are scale-invariant.
    // MSAVI / EVI2 / BAI carry additive constants anchored to unit
    // reflectance [0,1]; without a declared SICNU_NUMERIC_SCALE the kernels
    // fall back to the same documented magnitude heuristic as EVI/SAVI
    // (docs/processing/grid-and-radiometric-policy.md §2).

    /**
     * GNDVI = (NIR - Green) / (NIR + Green)
     */
    bool gndvi(const float *nir, const float *green, float *out, size_t count);

    /**
     * NDMI = (NIR - SWIR1) / (NIR + SWIR1)
     */
    bool ndmi(const float *nir, const float *swir1, float *out, size_t count);

    /**
     * ARVI = (NIR - RB) / (NIR + RB), RB = 2*Red - Blue (atmosphere-resistant
     * vegetation index, Kaufman & Tanré 1992).
     */
    bool arvi(const float *nir, const float *red, const float *blue, float *out, size_t count);

    /**
     * MSAVI = (2*NIR + 1 - sqrt((2*NIR + 1)^2 - 8*(NIR - Red))) / 2
     * (Qi et al. 1994, modified soil-adjusted vegetation index; unit-reflectance domain).
     */
    bool msavi(const float *nir, const float *red, float *out, size_t count);

    /**
     * EVI2 = 2.5 * (NIR - Red) / (NIR + 2.4*Red + 1)
     * (two-band EVI, Jiang et al. 2008; unit-reflectance constants, EVI regime rules).
     */
    bool evi2(const float *nir, const float *red, float *out, size_t count);

    /**
     * BAI = 1 / ((0.1 - Red)^2 + (0.06 - NIR)^2)
     * (burn area index, Chuvieco et al. 2002; unit-reflectance domain, unbounded
     * output that grows as bands approach the anchors).
     */
    bool bai(const float *red, const float *nir, float *out, size_t count);

    /**
     * UI = (SWIR2 - NIR) / (SWIR2 + NIR) (urban index, Kawamura et al. 1996).
     */
    bool ui(const float *swir2, const float *nir, float *out, size_t count);

    /**
     * BUI = NDBI - NDVI = (SWIR-NIR)/(SWIR+NIR) - (NIR-Red)/(NIR+Red)
     * (built-up index, Zha et al. 2003 / He et al. 2010 composition).
     */
    bool bui(const float *swir, const float *nir, const float *red, float *out, size_t count);

    // --- Foundation 6.0 (#801): explicit numeric-domain variants -------------
    // Streaming callers MUST resolve the numeric domain ONCE per raster
    // (processing/contracts/scientific_contracts.h), normalize inputs to
    // unit reflectance (÷divisor), and call the Unit forms — the index
    // constants never depend on tile content. The auto forms (evi, savi,
    // evi2, msavi, bai) decide the regime from the WHOLE buffer they
    // receive: pass a full frame and the decision is frame-consistent;
    // pass a streaming tile and adjacent tiles can disagree — that is the
    // #801 seam defect, do not call the auto forms from streaming loops.
    bool eviUnit(const float *nir, const float *red, const float *blue, float *out, size_t count);
    bool eviDn(const float *nir, const float *red, const float *blue, float *out, size_t count);
    bool saviUnit(const float *nir, const float *red, float *out, size_t count);
    bool saviDn(const float *nir, const float *red, float *out, size_t count);
    bool evi2Unit(const float *nir, const float *red, float *out, size_t count);
    bool evi2Dn(const float *nir, const float *red, float *out, size_t count);
    bool msaviUnit(const float *nir, const float *red, float *out, size_t count);
    bool msaviDn(const float *nir, const float *red, float *out, size_t count);
    bool baiUnit(const float *red, const float *nir, float *out, size_t count);
    bool baiDn(const float *red, const float *nir, float *out, size_t count);
}
