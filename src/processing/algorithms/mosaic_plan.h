// mosaic_plan.h — F15 Package A: scene graph / grid planning for quality
// mosaicking (ADR 0163).
//
// The planner turns per-scene metadata into a deterministic mosaic grid plan:
// union extent, reference resolution/CRS, per-scene grid placement, a
// back-to-front composite order, and a pairwise overlap inventory that drives
// downstream radiometric balancing (Package B) and seamline placement
// (Package C). It is a pure metadata analysis layer — no file I/O, no pixel
// access — so it is unit-testable without datasets and stays O(scenes).
//
// Failure semantics: build() fails closed (nullopt + message) only on
// geometric impossibilities (no scenes, zero/non-finite pixel size, zero or
// non-finite extent, bad reference index). Cross-CRS, rotated/sheared,
// pixel-size mismatch and sub-pixel offsets are *diagnostics* — the planner
// reports them and marks affected scenes grid-ineligible; the calling
// operator enforces the fail-closed policy. Mixed-CRS scenes get a footprint
// envelope transformed into the plan CRS so the diagnostic can name the
// exact reprojection target (see D-004: no inline reprojection here —
// compose with the existing gdal reproject operator first).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rs::mosaic {

struct SceneEntry {
    std::string path;
    int width = 0;
    int height = 0;
    std::array<double, 6> geoTransform{}; // GDAL order: originX, pxX, rotX, originY, rotY, pxY
    std::string crsWkt;                   // WKT (or empty when unknown)
    bool hasNodata = false;
    float nodata = 0.0f;
    int bandCount = 0;
    int priority = 0;                     // higher wins composite ties; then lower input index
};

struct ScenePlacement {
    int64_t offsetX = -1;
    int64_t offsetY = -1;
    bool valid = false;       // false when the scene cannot be placed on the grid
    bool aligned = false;     // integer-pixel alignment against the grid
    double fracOffsetX = 0.0; // fractional residual in pixels (0 when aligned)
    double fracOffsetY = 0.0;
};

struct ScenePlanEntry {
    SceneEntry scene;
    ScenePlacement placement;
    bool crsMatchesPlan = true;
    bool gridEligible = false; // !rotated && crsMatchesPlan && pixel size matches
};

struct OverlapPair {
    int a = -1;          // composite order index (a < b)
    int b = -1;
    int64_t pixels = 0;  // overlap area in plan-grid pixels
};

struct PlanDiagnostics {
    std::vector<int> rotated;         // input-order scene indices
    std::vector<int> crsMismatch;
    std::vector<int> pixelSizeMismatch;
    std::vector<int> subPixelOffset;
    std::vector<std::string> warnings; // actionable, one per condition class
};

struct MosaicPlan {
    std::string crsWkt;                   // plan (reference) CRS
    std::array<double, 6> gridTransform{}; // output geotransform on the plan grid
    int width = 0;
    int height = 0;
    std::vector<ScenePlanEntry> scenes;   // input order
    std::vector<int> compositeOrder;      // indices into scenes; back-to-front paint order
    std::vector<OverlapPair> overlaps;    // grid-eligible pairs with pixels > 0
    PlanDiagnostics diagnostics;
};

class MosaicPlanner {
  public:
    struct Options {
        int referenceIndex = -1;          // -1 = auto: highest priority, then lowest index
        double pixelSizeTolerance = 1e-6; // relative
        double subPixelEpsilon = 1e-6;    // pixels
    };

    static std::optional<MosaicPlan> build(const std::vector<SceneEntry>& scenes,
                                           const Options& options,
                                           std::string* errorMessage = nullptr);

    /// Conservative geographic envelope of a scene transformed from its own
    /// CRS into targetCrs: {minX, minY, maxX, maxY} of the four transformed
    /// corners. Accepts WKT or user-input CRS strings (e.g. "EPSG:32631").
    /// Returns nullopt when either CRS is unavailable or the transform fails.
    static std::optional<std::array<double, 4>>
    footprintEnvelope(const SceneEntry& scene, const std::string& targetCrs);

    /// CRS equality: string equality after trim, else OGR IsSame.
    static bool sameCrs(const std::string& wktA, const std::string& wktB);
};

} // namespace rs::mosaic
