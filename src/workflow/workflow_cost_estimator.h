// src/workflow/workflow_cost_estimator.h — Flops / Peak-RSS cost model (D17, ADR 0162)
#pragma once
//
//   Flops(v)    = W x H x B x K(operatorId)
//   PeakRSS     = max tier Σ (W x H x B x 4 B) + kBaseEngineOverheadBytes
//   Duration    = Σ_v Flops(v) / kFlopsPerSecond
//
// K(op) is a per-operator complexity coefficient (closed table; unknown
// operators default to 1.0). When PeakRSS exceeds 70 % of the reported
// host RAM, recommendedMaxParallelism degrades to 1 (memory waterline);
// otherwise it is the max tier width, capped at kDefaultMaxParallelism.
// Resolution facts come from @p rasterDimensions (node id -> W x H);
// nodes absent from the map use kFallbackRasterSize.
//

#include <QMap>
#include <QSize>

#include "workflow/plan_optimizer.h"
#include "workflow/workflow_ir_v2.h"

namespace sicnu::workflow {

class WorkflowCostEstimator
{
  public:
    WorkflowCostEstimator() = delete;

    /// Per-operator complexity coefficient (closed table, deterministic).
    static double operatorComplexity( const QString &operatorId );

    static CostEstimate estimatePipelineCost( const WorkflowDefinition &def,
                                              const QMap<QString, QSize> &rasterDimensions );

    /// Test seam: same model against an EXPLICIT host RAM budget, so the
    /// waterline behavior is pinned without depending on the live host.
    static CostEstimate estimatePipelineCostWithHostRam( const WorkflowDefinition &def,
                                                         const QMap<QString, QSize> &rasterDimensions,
                                                         qint64 hostTotalRamBytes );

    /// Bytes per band pixel used by the model (Float32).
    static constexpr qint64 kBytesPerPixel = 4;
    /// Fixed engine overhead added on top of the live tier working set.
    static constexpr qint64 kBaseEngineOverheadBytes = 64 * 1024 * 1024;
    /// Memory waterline: above this share of host RAM, parallelism -> 1.
    static constexpr double kRssWaterline = 0.70;
    static constexpr int kDefaultMaxParallelism = 2;
    static constexpr double kFlopsPerSecond = 200.0e6;
    static constexpr QSize kFallbackRasterSize{ 512, 512 };
};

} // namespace sicnu::workflow
