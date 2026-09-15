// multimodal_matcher.h — F13 Packages A + B: cross-modal window matching
// with a coarse-to-fine pyramid.
//
// What this is: a dependency-free, deterministic matcher for optical↔SAR /
// cross-sensor image pairs that returns *tie points with per-point trust
// scores and explicit refusal semantics*. It is not a generic feature
// detector; there is deliberately no SIFT/ORB path here (D14's
// rs::algorithms::FeatureMatcher covers single-modality descriptor
// matching and its RANSAC is reused as the consensus filter).
//
// Metrics (seam — callers choose; Auto is the multimodal default):
//   PhaseCorrelation  windowed FFT cross-power spectrum; invariant to a
//                     global radiometric gain; used for the coarse levels.
//   MutualInformation 16-bin normalized MI on robustly-quantized windows;
//                     invariant to any monotone remap; used for fine
//                     refinement (optical-SAR).
//   NormalizedCrossCorrelation  zero-mean NCC; for radiometrically similar
//                     sensors only.
//   Auto              phase correlation down the pyramid, mutual
//                     information at the finest level.
//
// Failure semantics (contract, see registration_types.h): structural
// shortfalls produce status=Refused (reason codes too_few_matches,
// flat_region, cancelled, resource_exhausted); evidence shortfalls produce
// status=LowConfidence (low_peak_snr, insufficient_coverage). A successful
// report always carries a consensus homography and per-point scores.
#pragma once

#include "registration_types.h"

#include <atomic>
#include <QString>
#include <vector>

namespace sicnu::registration {

enum class MatchMetric {
    Auto,
    PhaseCorrelation,
    MutualInformation,
    NormalizedCrossCorrelation
};

struct MultimodalMatchOptions {
    MatchMetric metric{MatchMetric::Auto};
    int windowSize{64};            // window side at the finest level (px, 16..256)
    int searchRadius{6};           // fine-level local search radius (px)
    int minMatches{8};             // structural floor for a usable match set
    // Per-metric per-point trust floor in [0, 1]: phase = snr/20, mutual
    // information = normalized information (~0.13 on strong speckled pairs,
    // ~0.02 for independent windows), NCC = the coefficient itself.
    double minScore{0.10};
    double minPeakSnr{8.0};        // phase peak / surface median floor
    double minValidFraction{0.6};  // fraction of valid (non-NaN) window samples
    int coverageGrid{4};           // coverage grid side over the FULL source extent
    double minCoverageRatio{0.35}; // populated coverage cells / total coverage cells
    double ransacReprojThreshold{3.0}; // consensus threshold at the finest level
    int ransacMaxIters{2000};
    double ransacConfidence{0.99};
    ResourceBounds bounds{};
};

struct PyramidStageEvidence {
    int level{0};
    int sourceWidth{0};
    int sourceHeight{0};
    int accepted{0};
    double medianScore{0.0};
};

struct MultimodalMatchReport {
    RegistrationStatus status{RegistrationStatus::Refused};
    QString reason; // snake_case code from registration_types.h; empty on Success
    std::vector<RegistrationPoint> points;
    std::vector<PyramidStageEvidence> stages;
    // Consensus homography (row-major, dst <- src) over inlier points;
    // identity when status != Success.
    std::vector<double> consensusHomography = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    int inlierCount{0};
    double inlierRatio{0.0};
    double inlierRmse{0.0};
    // Inlier-populated coverage cells / total coverage cells over the full
    // source extent (coverageGrid x coverageGrid). A spatially clustered
    // match set scores low — this is the Oracle against clustered-GCP
    // overconfidence.
    double coverageRatio{0.0};
    int rejectedFlatWindows{0};
    int rejectedLowSnrWindows{0};
};

class MultimodalMatcher {
  public:
    /// Match two row-major float images (NaN = NoData). Pixel centers are at
    /// integer coordinates. Never throws for data conditions — structural
    /// problems are reported through MultimodalMatchReport::status/reason.
    /// Throws std::invalid_argument only for null buffers or non-positive
    /// dimensions.
    static MultimodalMatchReport matchImages(const float* srcData, int srcWidth, int srcHeight,
                                             const float* dstData, int dstWidth, int dstHeight,
                                             const MultimodalMatchOptions& options = {},
                                             const std::atomic_bool* cancel = nullptr);

    /// Logical scratch estimate for the padded FFT/MIO buffers of one call
    /// (MiB). Exposed so tests can pin the memory contract without measuring.
    static double estimateScratchMiB(int srcWidth, int srcHeight, int dstWidth, int dstHeight,
                                     const MultimodalMatchOptions& options);
};

} // namespace sicnu::registration
