// mosaic_seamline.h — F15 Package C: seamline placement for quality mosaics
// (ADR 0163).
//
// A seamline is the per-pixel boundary inside a scene-pair overlap that
// decides which scene contributes each pixel. The seam minimizes a cost
// surface through a dynamic-programming min-cost path:
//
//   cost = w.radiometric · |A − B|            (balanced radiometric difference)
//        + w.gradient     · |∇A − ∇B|         (edge/texture disagreement)
//        + w.cloud        · max(penA, penB)   (cloud/quality penalty, optional)
//        + w.edgeDistance · d_norm            (keep seams away from footprints)
//
// The path orientation follows the pair geometry: vertical seams for
// side-by-side overlaps (one column per row), horizontal seams for stacked
// overlaps (one row per column). Equal-cost ties break to the smaller
// column/row, then to the smaller predecessor offset — fully deterministic
// for a given cost surface.
//
// Memory: the exact kernel is O(w·h) for the cost surface supplied by the
// caller (used on small/window surfaces and in tests); production callers
// use the binned builder, which streams the overlap and reduces the DP to a
// ≤512×512 cell grid, so memory scales with the seam band, not the scene
// size (Oracle: large-image memory grows with tile, not image).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rs::mosaic {

enum class SeamOrientation { Vertical, Horizontal };

struct SeamCostWeights {
    double radiometric = 1.0;
    double gradient = 0.5;
    double cloud = 2.0;
    double edgeDistance = 1.0;
};

/// Min-cost seam through a cost surface (row-major width*height floats).
/// Vertical: returns one column per row (size height). Horizontal: one row
/// per column (size width). Returns an empty vector on invalid input.
std::vector<int> computeSeamPath( const std::vector<float> &cost, int width, int height,
                                  SeamOrientation orientation, double *totalCost = nullptr );

/// Sign of the per-pixel side assignment for a pair. For a vertical seam,
/// side +1 means "scene A is left of the seam" (pixels with column < seam
/// belong to A); side -1 inverts. For horizontal seams the same applies per
/// row with "A above".
struct SeamDecision {
    SeamOrientation orientation = SeamOrientation::Vertical;
    std::vector<int> path; // seam column per row (vertical) or row per column (horizontal)
    int aSide = +1;        // which side of the path belongs to scene A
    double totalCost = 0.0;
};

/// Decides which side of the pair overlap belongs to A from the pair's
/// grid placement (deterministic geometric rule: A is the left/top side when
/// A's placement offset is smaller; orientation follows the larger offset
/// delta axis).
SeamDecision decideSeam( const std::vector<float> &cost, int width, int height,
                         SeamOrientation orientation, bool aOnNegativeSide );

/// Streams an overlap rectangle window by window and accumulates a binned
/// cost surface (mean cost per cell). DP then runs on the binned grid, so
/// the working set stays bounded regardless of scene size.
class BinnedSeamCost {
  public:
    BinnedSeamCost( int64_t overlapWidth, int64_t overlapHeight, SeamCostWeights weights,
                    int maxCells = 512 );

    /// Add one overlap window at overlap-local offset (x, y), size w*h:
    /// sceneA/sceneB are balanced reflectances at the same window (row-major),
    /// cloudPenA/cloudPenB optional (empty = none). NaN marks invalid pixels
    /// (excluded from means). Windows may arrive in any order/coverage;
    /// unvisited cells stay at the invalid-cell cost.
    void addWindow( int64_t x, int64_t y, int64_t w, int64_t h,
                    const std::vector<float> &sceneA, const std::vector<float> &sceneB,
                    const std::vector<float> &cloudPenA = {},
                    const std::vector<float> &cloudPenB = {} );

    /// Finish accumulation and solve the binned DP. Returns per-row (or
    /// per-column) seam path in *cell* coordinates; scale to pixels when
    /// consuming (cell c covers pixels [c*cellW, (c+1)*cellW)).
    SeamDecision solve();

    int64_t cellsX() const { return cellsX_; }
    int64_t cellsY() const { return cellsY_; }

  private:
    int64_t overlapW_ = 0, overlapH_ = 0;
    int64_t cellsX_ = 0, cellsY_ = 0;
    int64_t cellW_ = 1, cellH_ = 1;
    SeamCostWeights weights_;
    // Per-cell accumulators for the four cost terms (sums + counts for means).
    std::vector<double> sumRad_, sumGrad_, sumCloud_, sumEdge_;
    std::vector<int64_t> count_;
};

} // namespace rs::mosaic
