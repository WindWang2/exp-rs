// fusion_quality_report.h — F15 Package F: fusion quality report artifact and
// spectral-distortion guard (ADR 0163).
//
// Master already ships the fusion kernels and the Wald memory metrics
// (rs::algorithms::PanSharpening, ADR 0159). What this module adds on top:
//   • Q — Wang & Bovik universal image quality index (8×8 windows)
//   • RASE — Relative Average Spectral Error
//   • per-band mean/std ratios (degraded fused vs original MS) as the
//     spectral-distortion signature
//   • a pass/fail guard with explicit, configurable thresholds
//   • a JSON artifact (serialized with jsoncpp inside sicnu_processing;
//     the header stays dependency-free)
//
// Inputs follow the Wald protocol: the caller fuses at reduced resolution
// (degraded pan) and hands the *degraded fused bands* plus the *original MS
// bands* (same grid) to evaluateFusionQuality. All statistics are computed
// here in one pass per band with O(bands) extra memory.
#pragma once

#include <string>
#include <vector>

namespace rs::fusion {

struct FusionQualityThresholds {
    double maxErgas = 2.5;
    double minMeanCc = 0.94;
    double minQ = 0.80;
    double maxMeanRatioDeviation = 0.05; // |mean(degraded)/mean(original) − 1|
    double maxStdRatioDeviation = 0.10;  // |std(degraded)/std(original) − 1|
};

struct FusionQualityReport {
    // Wald metrics (ERGAS / mean CC / RMSE / SSIM from the ADR 0159 kernel).
    double ergas = 0.0;
    double meanCc = 0.0;
    double rmse = 0.0;
    double ssim = 0.0;
    // Additional quality indices.
    double qIndex = 0.0; // mean universal image quality index across bands
    double rase = 0.0;   // relative average spectral error
    // Spectral-distortion signature.
    std::vector<double> meanRatio; // degraded/original per band
    std::vector<double> stdRatio;
    // Guard verdict.
    bool passed = false;
    std::vector<std::string> violations; // human-readable, empty when passed

    /// Deterministic JSON serialization (field order fixed).
    std::string toJson() const;
};

/// Evaluate fusion quality. Returns a report whose `passed` flag reflects
/// the thresholds; degenerate inputs (mismatched band counts, empty bands)
/// produce a failing report with a violation naming the problem rather than
/// an exception, so callers can surface it as operator input errors.
FusionQualityReport evaluateFusionQuality( const std::vector<const float *> &degradedFusedBands,
                                           const std::vector<const float *> &originalMsBands,
                                           int width, int height, double scaleRatio,
                                           const FusionQualityThresholds &thresholds = {} );

/// Streaming variant of evaluateFusionQuality for out-of-core callers: feed
/// aligned reference (y) vs evaluated (x) windows band by band, then finalize.
/// Windows should be 8-aligned (Q uses non-overlapping 8×8 windows on the
/// global grid); partial edge windows are dropped per the standard Q
/// definition. Memory is O(1) in the image size.
class FusionQualityAccumulator {
  public:
    void beginBand();
    /// x = evaluated (fused) window, y = reference window, row-major w*h.
    void addWindow( const float *x, const float *y, int w, int h );
    void endBand();

    /// bandCount/width/height describe the full rasters (for degenerate-input
    /// violations and RASE normalization), not the window sizes.
    FusionQualityReport finalize( int bandCount, int width, int height, double scaleRatio,
                                  const FusionQualityThresholds &thresholds = {} );

  private:
    struct BandAccum
    {
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, mse = 0, qSum = 0;
        double maxY = 0; // running |max| of the reference band (SSIM's L)
        int64_t n = 0;
        int64_t qWindows = 0;
    };
    BandAccum band_;
    std::vector<BandAccum> bands_;
};

/// Write text atomically: temp file in the target directory + fsync-style
/// close + rename. Returns false with a message when the target directory is
/// not writable or the rename fails.
bool writeTextFileAtomic( const std::string &path, const std::string &content,
                          std::string *errorMessage = nullptr );

} // namespace rs::fusion
