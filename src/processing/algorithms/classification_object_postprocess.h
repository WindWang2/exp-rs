// classification_object_postprocess.h — Classification & Object Intelligence
// 11.0 (F12, WP-E). Object-level (segment-aware) thematic cleanup.
//
// Distinct from the D15 pixel-level seam (classification_postprocess.h):
// here the unit of manipulation is the SEGMENT, identified by a segment-id
// raster and classified through a per-segment majority vote of the class
// labels. The conventional NoData label follows the D15 sentinel (-1) and
// never absorbs or merges into thematic classes (audit gap #14).
//
// Pipeline (ClassificationObjectPostProcessor::run):
//   1. per-segment majority class + area (ties → lowest class id, the
//      rs_majority_vote convention);
//   2. segment adjacency graph over the segment raster (4/8 connectivity),
//      edge weight = shared-border pixel count;
//   3. min-area rule: segments with area < minSegmentArea adopt the class
//      of the adjacent segment sharing the longest border (ties → lowest
//      class id, then lowest segment id); merges cascade deterministically
//      (smallest offender first, one merge per pass);
//   4. iterative adjacency smoothing: a segment adopts a neighbour class
//      only when some class holds STRICTLY more than half of the classed
//      neighbour border (Jacobi updates — every decision within one
//      iteration reads the previous iteration's classes, so results are
//      deterministic regardless of raster scan order);
//   5. paint: every pixel receives its segment's final class; NoData
//      segments (id <= 0) paint -1.
//
// Bounded by construction: segment count above maxSegments is a typed
// refusal (no silent degradation).
#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>

namespace rs::processing
{

struct ObjectPostProcessConfig
{
    int minSegmentArea{ 0 };        ///< merge segments smaller than this
    int smoothingIterations{ 0 };   ///< Jacobi strict-majority passes (0 = off)
    int maxSegments{ 2000000 };     ///< typed refusal above this segment count
    int connectivity{ 8 };          ///< 4 or 8 (any value other than 4 is 8)
};

class ClassificationObjectPostProcessor
{
  public:
    struct SegmentNode
    {
        int majorityClass = -1;   ///< per-pixel majority class label
        int area = 0;             ///< pixel count
    };

    /// segment id -> (majority class, area). Segment ids <= 0 are NoData and
    /// excluded from the table. A pixel with label -1 never contributes a
    /// class vote (but counts towards area only when its segment has other
    /// valid pixels... no — area counts every pixel of the segment in the
    /// segment raster; class votes come from label != -1 pixels only).
    using SegmentTable = std::unordered_map<int, SegmentNode>;

    /// adjacency[a][b] = shared border pixel count between segments a and b.
    using Adjacency = std::unordered_map<int, std::unordered_map<int, int>>;

    struct RunStats
    {
        int segmentCount = 0;
        int mergedSegments = 0;    ///< segments removed by the min-area rule
        int smoothedSegments = 0;  ///< segments that changed class in smoothing
    };

    struct RunResult
    {
        bool ok = false;
        /// Typed refusal reason when !ok (segment cap exceeded, bad sizes,
        /// non-integer connectivity…). Empty when ok.
        struct Error
        {
          enum class Code
          {
            None = 0,
            SizeMismatch,
            TooManySegments,
          };
          Code code = Code::None;
        };
        Error error;
        RunStats stats;
    };

    /// Per-segment majority class + area. Tie → lowest class id.
    static SegmentTable computeSegmentClasses( std::span<const int> labels,
                                               std::span<const int> segments );

    /// Segment adjacency over a row-major segment raster.
    static Adjacency buildAdjacency( std::span<const int> segments,
                                     int width, int height, int connectivity );

    /// Min-area merge: returns the final segment-id → class table (merged
    /// donors are absent or carry their absorbing class; the paint step only
    /// consults the target segment of each pixel, so absent donors are fine
    /// when the absorber shares every pixel — donors disappear only when
    /// their pixels are re-segmented, which never happens here, so merged
    /// donors KEEP an entry equal to the absorbing class).
    static std::unordered_map<int, int> applyMinAreaRule( const SegmentTable &table,
                                                          const Adjacency &adjacency,
                                                          int minSegmentArea,
                                                          int *mergedCount = nullptr );

    /// Iterative strict-majority smoothing over the classed adjacency graph.
    static std::unordered_map<int, int> applySmoothing( const std::unordered_map<int, int> &classes,
                                                        const Adjacency &adjacency,
                                                        int iterations,
                                                        int *changedCount = nullptr );

    /// Full pipeline; writes a row-major label raster of identical size.
    /// \a outLabels must hold the same number of cells as \a labels.
    static RunResult run( std::span<const int> labels,
                          std::span<const int> segments,
                          int width, int height,
                          const ObjectPostProcessConfig &config,
                          std::span<int> outLabels );
};

} // namespace rs::processing
