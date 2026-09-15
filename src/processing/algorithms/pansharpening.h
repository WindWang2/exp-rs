// pansharpening.h — D14 Package F: pan-sharpening algorithms and Wald-protocol
// quality metrics (ADR 0159).
//
// Methods: Gram-Schmidt (Aiazzi/Laben projection-substitution form), Brovey,
// IHS intensity substitution, and high-pass-filter addition. All methods
// bilinearly upsample the multispectral bands to the panchromatic grid first.
//
// Quality: ERGAS = 100·scaleRatio·sqrt(mean((RMSE_k/mu_k)^2)) — the Wald
// protocol degrades the fused product back to the MS scale and compares it
// with the original bands (performed by the caller); CC is the per-band
// Pearson correlation; SSIM uses the global (mean/variance/covariance) form.
#pragma once

#include <vector>

namespace rs::algorithms {

enum class PanSharpenMethod {
    GramSchmidt,
    Brovey,
    Ihs,
    Hpf // High-Pass Filter
};

struct PanSharpenMetrics {
    double ergas{0.0};  // relative dimensionless global error (<= 2.5 target)
    double meanCc{0.0}; // mean correlation coefficient (>= 0.94 target)
    double rmse{0.0};   // mean per-band RMSE
    double ssim{0.0};   // mean structural similarity
};

class PanSharpening {
  public:
    /// Fuse low-resolution multispectral bands with a high-resolution pan
    /// band. outSharpenedBands must hold one float* per band, each already
    /// allocated at panW x panH. bandWeights (length = band count, or empty
    /// for equal weights) drive the simulated low-resolution pan of the GS
    /// method. Requires >= 3 bands and an integer pan/ms resolution ratio.
    static bool sharpen(PanSharpenMethod method,
                        const std::vector<const float*>& msBands,
                        int msWidth, int msHeight,
                        const float* panBand,
                        int panWidth, int panHeight,
                        std::vector<float*>& outSharpenedBands,
                        const std::vector<double>& bandWeights = {});

    /// Wald-protocol metrics of degraded fused bands against the original
    /// multispectral reference. scaleRatio is h/l (e.g. 0.25 for a 4x fusion).
    static PanSharpenMetrics evaluateQuality(const std::vector<const float*>& degradedFusedBands,
                                             const std::vector<const float*>& originalMsBands,
                                             int width, int height,
                                             double scaleRatio);
};

} // namespace rs::algorithms
