// src/processing/algorithms/glcm_texture.h — D15 Package B public seam.
//
// Gray-Level Co-occurrence Matrix (GLCM) texture features: Haralick metrics
// over a sliding window with configurable direction / step / quantization.
// Pure STL in/out (span / vector / float*) so the workbench, the agent
// diagnostics and the E2E harness share one seam.  NaN pixels quantize to
// the low bin for pair counting but make the *window's* metrics NaN when
// the window contains any non-finite sample (sentinel transparency).
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace rs::processing
{

enum class GlcmDirection
{
    Deg0 = 0,            ///< horizontal offset (dx=+d, dy=0)
    Deg45 = 1,           ///< diagonal offset    (dx=+d, dy=-d)
    Deg90 = 2,           ///< vertical offset    (dx=0,  dy=+d)
    Deg135 = 3,          ///< anti-diagonal      (dx=-d, dy=-d)
    Omnidirectional = 4, ///< mean of the four directional normalized GLCMs
};

struct GlcmConfig
{
    int windowSize{ 5 }; ///< odd: 3, 5, 7, 9 (even values snap DOWN to the nearest odd)
    int stepDistance{ 1 };
    GlcmDirection direction{ GlcmDirection::Omnidirectional };
    int quantLevels{ 32 }; ///< equal-width bins over [minVal, maxVal]
    float minVal{ 0.0f };
    float maxVal{ 1.0f };
    bool isSymmetric{ true }; ///< symmetrize the co-occurrence counts
};

struct GlcmHaralickMetrics
{
    double contrast{ 0.0 };
    double dissimilarity{ 0.0 };
    double homogeneity{ 0.0 };
    double energy{ 0.0 };
    double entropy{ 0.0 };
    double angularSecondMoment{ 0.0 };
    double mean{ 0.0 };
    double variance{ 0.0 };
    double correlation{ 0.0 };
};

class GlcmTextureCalculator
{
  public:
    /// Haralick metrics of one window (@p winWidth x @p winHeight row-major
    /// samples).  Returns NaN-poisoned metrics for empty/invalid input or a
    /// window containing non-finite samples.
    static GlcmHaralickMetrics computeForWindow( std::span<const float> windowPixels,
                                                 int winWidth,
                                                 int winHeight,
                                                 const GlcmConfig &config );

    /// Whole-raster sliding-window feature map (row-major, width*height).
    /// Borders use clamp replication.  @p featureName is one of
    /// "contrast" | "dissimilarity" | "homogeneity" | "energy" | "entropy" |
    /// "asm" | "mean" | "variance" | "correlation"; unknown names yield an
    /// all-NaN map.
    static std::vector<float> computeTextureFeatureMap( const float *rasterData,
                                                        int width,
                                                        int height,
                                                        const GlcmConfig &config,
                                                        const std::string &featureName );

    /// Teaching/diagnostic seam: the symmetrized normalized co-occurrence
    /// matrix P(i,j) (row-major, levels*levels) for one window, with the
    /// quantization level count in @p outLevels.  Sum(P) == 1 for any
    /// window with at least one counted pair (R > 0).
    static std::vector<double> normalizedCooccurrence( std::span<const float> windowPixels,
                                                       int winWidth,
                                                       int winHeight,
                                                       const GlcmConfig &config,
                                                       int &outLevels );
};

} // namespace rs::processing
