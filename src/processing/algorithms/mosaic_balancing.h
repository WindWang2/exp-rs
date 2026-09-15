// mosaic_balancing.h — F15 Package B: inter-scene radiometric balancing for
// quality mosaics (ADR 0163).
//
// Purpose: make adjacent scenes with different illumination/sensor conditions
// statistically consistent before seamline placement and blending, without
// touching absolute calibration (that is the radiometric-calibration domain,
// kept strictly separate from this module).
//
// Method (D-005): for every overlap pair in the MosaicPlan, fit a robust
// linear model y ≈ α·x + β on the overlap samples — x = child scene raw
// values, y = parent-corrected values of the already-chained parent — so the
// fitted (α, β) IS the child's cumulative correction (no extra composition).
// The fit is seeded by least-median-of-squares over a deterministic strided
// subsample (tolerates clustered cloud contamination) and refined by up to
// two residual-MAD inlier refits; clean overlaps converge after one
// refinement. Scenes are chained from the reference scene by BFS through the
// overlap graph. Scenes whose cumulative gain leaves [minGain, maxGain],
// whose bias exceeds maxAbsBiasSigma × (|meanY| + stdY) of the overlap, or
// which cannot reach the reference through the overlap graph, are rejected
// (fail-closed by default, droppable by policy).
//
// Memory: statistics only — O(bands) accumulators; pixel samples flow through
// caller-provided windows and are never stored.
#pragma once

#include "mosaic_plan.h"

#include <string>
#include <vector>

namespace rs::mosaic {

struct BalanceGain {
    double gain = 1.0;
    double bias = 0.0;

    double apply( double v ) const { return gain * v + bias; }
};

struct SceneBalance {
    int scene = -1;                       // input-order index
    int parentScene = -1;                 // BFS parent (-1 for the reference)
    int referenceHop = 0;                 // 0 = reference
    int64_t supportPixels = 0;            // inlier overlap pixels backing the fit
    std::vector<BalanceGain> perBand;     // one entry per band (identity for the reference)
    bool rejected = false;
    std::string rejectReason;             // empty unless rejected
};

struct BalancingOptions {
    int referenceScene = -1;   // -1 = auto: largest eligible pixel count, tie -> lowest index
    double minGain = 0.5;      // cumulative gain clamp (per band)
    double maxGain = 2.0;
    double maxAbsBiasSigma = 3.0; // |bias| limit in units of (|overlap mean| + overlap std)
    double inlierK = 2.5;         // inlier threshold in residual MAD units
    int64_t minOverlapPixels = 64;
    int windowSize = 256;         // sampling window edge (memory bound)
    // Policy for rejected scenes: "fail" = balance() returns false naming the
    // scene (fail-closed default); "drop" = proceed without the scene.
    enum class RejectPolicy { Fail, Drop };
    RejectPolicy rejectPolicy = RejectPolicy::Fail;
};

/// Streaming sampler: yields a scene's band values on plan-grid windows.
/// Implementations own NoData semantics — invalid pixels are returned as NaN.
class OverlapSampler {
  public:
    virtual ~OverlapSampler() = default;
    /// Read band `band` of scene `scene` (input-order index) for the grid
    /// window [x0, x0+w) x [y0, y0+h) into `out` (row-major, w*h floats).
    virtual bool readGridWindow( int scene, int band, int64_t x0, int64_t y0,
                                 int64_t w, int64_t h, std::vector<float> &out ) = 0;
};

class RadiometricBalancer {
  public:
    /// Fit cumulative per-band corrections for every grid-eligible scene.
    /// On RejectPolicy::Fail returns false with a message naming the first
    /// rejected scene; on Drop, rejected scenes carry rejected=true.
    static bool balance( const MosaicPlan &plan, OverlapSampler &sampler, int bandCount,
                         const BalancingOptions &options, std::vector<SceneBalance> *out,
                         std::string *errorMessage = nullptr );

    /// Number of grid-eligible scenes (placement.valid) — scenes rejected or
    /// ineligible must be excluded from compositing by the caller.
    static bool usable( const SceneBalance &b ) { return !b.rejected; }
};

} // namespace rs::mosaic
