// mosaic_quality.h — F15 Package E: quality scoring for composite mosaics
// (ADR 0163).
//
// Each scene receives a deterministic 0..1 score from the dimensions it
// provides: cloudiness (mean cloud fraction, lower is better), an external
// quality raster aggregate (higher is better), acquisition time closeness to
// a reference epoch, and off-nadir flatness. Weights are renormalized over
// the provided dimensions so scenes with partial metadata stay comparable;
// the score feeds composite ordering (seamline mode) and per-pixel winner
// selection (quality mode). Per-pixel provenance lives in the operator —
// this module is the scoring authority.
#pragma once

#include <vector>

namespace rs::mosaic {

struct SceneQualityInput {
    bool hasCloud = false;
    double cloudFraction = 0.0; // 0 = clear, 1 = fully cloudy
    bool hasQuality = false;
    double quality = 0.0;       // external quality score, 0..1 (higher better)
    bool hasTime = false;
    double timeDays = 0.0;      // acquisition offset from the epoch, days
    bool hasView = false;
    double viewAngleDeg = 0.0;  // absolute off-nadir angle
};

struct QualityWeights {
    double cloud = 0.5;
    double quality = 0.25;
    double time = 0.15;
    double view = 0.10;
};

struct QualityOptions {
    double timeScaleDays = 365.0; // score 0 at this many days from the epoch
    double viewScaleDeg = 45.0;   // score 0 at this absolute angle
};

struct QualityScore {
    double score = 0.0;      // 0..1
    bool clamped = false;    // true when an input fell outside its assumed range
};

class QualityScorer {
  public:
    /// Score one scene. Values outside their nominal range are clamped into
    /// it (and reported via QualityScore::clamped) instead of poisoning the
    /// composite.
    static QualityScore score( const SceneQualityInput &input, const QualityWeights &weights,
                               const QualityOptions &options );

    /// Ordering by score descending; ties break by higher priority first
    /// (same "higher wins" convention as MosaicPlanner) then lower input
    /// index — deterministic and documented.
    static std::vector<int> compositeOrder( const std::vector<QualityScore> &scores,
                                            const std::vector<int> &priorities );
};

} // namespace rs::mosaic
