// src/core/spatial_split.h — D15 Package A public seam.
//
// Spatial leakage defense: block partitioning of georeferenced samples into
// Train / Validation / Test roles with an exclusive guard buffer, plus a
// Moran's I spatial autocorrelation auditor.  Pure STL — no Qt, no GDAL —
// so the classifier engine, the workbench and the test harness can all
// consume the seam without dependency direction violations.
//
// Isolation invariant (post-condition of partition()):
//   for every pair (train, eval) with eval in Validation|Test,
//   ||p_train - p_eval||_2 > config.bufferDistance   (strict)
// Samples inside the guard ring are demoted to ExcludedBuffer and belong to
// no evaluation set.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace rs::core
{

enum class SampleRole : uint8_t
{
    Unassigned = 0,
    Train = 1,
    Validation = 2,
    Test = 3,
    ExcludedBuffer = 4,
};

struct SpatialSamplePoint
{
    int64_t id{ 0 };
    double x{ 0.0 };
    double y{ 0.0 };
    int label{ 0 };
    std::vector<float> features;
};

struct SpatialBlockConfig
{
    double blockWidth{ 100.0 };
    double blockHeight{ 100.0 };
    double bufferDistance{ 20.0 };
    double trainRatio{ 0.6 };
    double valRatio{ 0.2 };
    double testRatio{ 0.2 };
    uint32_t randomSeed{ 42 };
};

struct SpatialSplitReport
{
    std::vector<SampleRole> assignments;
    size_t trainCount{ 0 };
    size_t valCount{ 0 };
    size_t testCount{ 0 };
    size_t bufferCount{ 0 };
    double minTrainTestDistance{ 0.0 };
    double spatialAutocorrelationMoranI{ 0.0 };
};

/// Splits @p samples into spatially isolated roles (see ADR 0160).
/// Empty input or a non-positive block size yields an empty report.
/// Deterministic for a fixed config.
class SpatialBlockPartitioner
{
  public:
    SpatialBlockPartitioner() = default;
    ~SpatialBlockPartitioner() = default;

    SpatialSplitReport partition( std::span<const SpatialSamplePoint> samples,
                                  const SpatialBlockConfig &config ) const;
};

/// Global Moran's I with inverse-distance weights hard-cut at
/// @p spatialCutoffDistance.  Degenerate inputs (N < 2, constant attribute,
/// no neighbour pairs inside the cutoff) return 0.0 — never NaN/inf.
class SpatialAutocorrelationAuditor
{
  public:
    static double computeMoransI( std::span<const SpatialSamplePoint> points,
                                  std::span<const double> attributeValues,
                                  double spatialCutoffDistance );
};

} // namespace rs::core
