// src/analysis/hyperspectral/continuum_removal.h — convex-hull continuum & absorption features (D13)
#pragma once

#include <cstddef>
#include <vector>

namespace exp_spectral
{
    /// Parameterized absorption feature extracted from a continuum-removed
    /// spectrum Rc(λ) ∈ (0, 1].
    struct SpectralAbsorptionFeature
    {
        double centerWavelengthNm = 0.0;    ///< Band center lambda_0 at minimum Rc
        double absorptionDepth = 0.0;       ///< Band depth D = 1 - min(Rc)
        double fwhmNm = 0.0;                ///< Full Width at Half Maximum (nm)
        double leftHalfWavelengthNm = 0.0;  ///< Left lambda where Rc = 1 - D/2
        double rightHalfWavelengthNm = 0.0; ///< Right lambda where Rc = 1 - D/2
        double bandArea = 0.0;              ///< Area of the absorption trough (nm)
        double asymmetry = 0.0;             ///< Ratio: (lambda_0 - left) / (right - lambda_0)
    };

    /// Hyperspectral continuum removal (Clark & Roush 1984).
    class ContinuumRemoval
    {
      public:
        /// Computes the upper convex hull continuum envelope C(λ) (Andrew
        /// monotone chain over the (λ, R) points) and the normalized spectrum
        /// Rc = R / C ∈ (0, 1], with Rc ≡ 1 at every hull vertex.
        /// Wavelengths must be finite and strictly increasing. NoData
        /// reflectance samples are excluded from the hull and their
        /// normalized output is set to @p noData. C <= 0 collapses to NaN.
        static bool compute( const float *wavelengths, const float *reflectance, size_t bandCount,
                             float *continuumOut, float *normalizedOut, float noData = -9999.0f );

        /// Identifies distinct absorption features from the normalized
        /// continuum-removed spectrum: each contiguous run below 1.0 yields
        /// one feature (center = argmin, depth = 1 - min, FWHM by linear
        /// interpolation at half depth, area by trapezoidal integration of
        /// 1 - Rc over the run). Runs shallower than @p minDepthThreshold
        /// are ignored. A flat spectrum yields no features.
        static std::vector<SpectralAbsorptionFeature> extractFeatures( const float *wavelengths,
                                                                       const float *normalized,
                                                                       size_t bandCount,
                                                                       double minDepthThreshold = 0.02 );
    };

} // namespace exp_spectral
