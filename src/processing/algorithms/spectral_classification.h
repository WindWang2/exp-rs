// spectral_classification.h — Hyperspectral analysis kernels.
//
// Pure-float, dependency-free spectral algorithms:
//   - Spectral Angle Mapper (SAM): supervised classification by computing the
//     angular distance between a pixel spectrum and each class reference
//     spectrum; the pixel is labelled to the smallest-angle class.
//   - Continuum Removal: normalizes a reflectance spectrum to its convex hull
//     (upper-envelope) tie-line so absorption features become comparable
//     across spectra of different brightness.
//
// All functions operate on raw float arrays with nodata support and follow the
// project convention of NaN/nodata for undefined output.
#pragma once

#include <cstddef>
#include <vector>

namespace SpectralClassification
{
    /**
     * Spectral Angle between two equal-length spectra t (test) and r (reference).
     *   theta = arccos( (t . r) / (||t|| * ||r||) )
     * Returns the angle in radians in [0, pi/2]; for non-negative reflectance
     * vectors the result lies in [0, pi/2]. Returns NaN if either vector has
     * zero norm or contains a nodata value.
     */
    double spectralAngle( const float *t, const float *r, size_t bands, float nodata );

    /**
     * Spectral Angle Mapper (SAM) classification.
     *
     * @param pixels     pixel-major spectra: pixels[p * bands + b], p in [0, count)
     * @param count      number of pixels
     * @param bands      number of spectral bands per pixel
     * @param refs       reference spectra: refs[c * bands + b]
     * @param refCount   number of reference spectra (= number of classes)
     * @param labels     [out] class label per pixel in [0, refCount-1], or -1
     *                   when the pixel is nodata or all angles are undefined
     * @param angles     [out, optional] best (minimum) angle per pixel, or NaN
     * @param nodata     nodata sentinel; any nodata band value marks the pixel
     *                   as nodata
     * @return true on success, false if any argument is invalid (zero pixels,
     *         zero bands, zero refs, or null pointers)
     */
    bool samClassify( const float *pixels, size_t count, int bands,
                      const float *refs, int refCount,
                      int *labels, float *angles,
                      float nodata );

    /**
     * Continuum removal on a single spectrum.
     *
     * Builds the convex upper hull (continuum) by the standard incremental
     * convex-hull-on-points algorithm over (band_index, reflectance), then
     * divides each reflectance sample by the continuum value at that band.
     * Output values lie in (0, 1] with 1.0 at the hull vertices (absorption
     * features become valleys).
     *
     * @param spectrum  input reflectance spectrum (length `bands`)
     * @param out       output buffer (length `bands`)
     * @param bands     number of samples
     * @param nodata    nodata sentinel; if any sample equals nodata the whole
     *                  spectrum is rejected (out filled with nodata)
     * @return true on success, false on invalid arguments or all-nodata input
     */
    bool continuumRemoval( const float *spectrum, float *out, int bands, float nodata );
}
