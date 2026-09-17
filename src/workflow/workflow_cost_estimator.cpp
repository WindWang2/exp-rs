// src/workflow/workflow_cost_estimator.cpp — analytic cost model (D17)
#include "workflow/workflow_cost_estimator.h"

#include "workflow/workflow_dag_analyzer.h"

#include <algorithm>

#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#endif

namespace sicnu::workflow {
namespace {

qint64 hostTotalRamBytes()
{
#ifdef Q_OS_LINUX
    struct sysinfo info;
    if ( ::sysinfo( &info ) == 0 )
        return qint64( info.totalram ) * qint64( info.mem_unit );
#endif
    return 8LL * 1024 * 1024 * 1024; // conservative default (8 GiB)
}

QSize rasterSizeFor( const QString &nodeId, const QMap<QString, QSize> &dimensions )
{
    const auto it = dimensions.constFind( nodeId );
    return it != dimensions.cend() ? it.value() : WorkflowCostEstimator::kFallbackRasterSize;
}

qint64 tierWorkingSetBytes( const QVector<ConcurrencyTier> &tiers, const WorkflowDocument &def,
                            const QMap<QString, QSize> &dimensions )
{
    qint64 peak = 0;
    for ( const ConcurrencyTier &tier : tiers )
    {
        qint64 working = 0;
        for ( const QString &nodeId : tier.nodeIds )
        {
            if ( const NodeFact *node = def.findNode( nodeId ) )
            {
                const QSize size = rasterSizeFor( nodeId, dimensions );
                working += qint64( size.width() ) * qint64( size.height() )
                           * qint64( std::max( 1, node->outputPorts.isEmpty() ? 1 : std::max( 1, node->outputPorts.first().bandCount ) ) )
                           * WorkflowCostEstimator::kBytesPerPixel;
            }
        }
        peak = std::max( peak, working );
    }
    return peak;
}

} // namespace

double WorkflowCostEstimator::operatorComplexity( const QString &operatorId )
{
    // Closed coefficient table. Deterministic; unknown ops default to 1.0.
    if ( operatorId == QLatin1String( "rs:import_raster" ) )
        return 0.5;
    if ( operatorId == QLatin1String( "rs:spectral_index" ) )
        return 2.0;
    if ( operatorId == QLatin1String( "rs:spatial_filter" ) )
        return 9.0; // k x k convolution
    if ( operatorId == QLatin1String( "rs:reproject" ) )
        return 6.0;
    if ( operatorId == QLatin1String( "rs:resample" ) )
        return 4.0;
    if ( operatorId == QLatin1String( "rs:radiometric_calibration" ) )
        return 1.0;
    if ( operatorId == QLatin1String( "rs:atmospheric_correction" ) )
        return 3.0;
    if ( operatorId == QLatin1String( "rs:threshold" ) )
        return 1.0;
    if ( operatorId == QLatin1String( "rs:gs_fusion" ) )
        return 12.0;
    if ( operatorId == QLatin1String( "rs:whittaker_smooth" ) )
        return 5.0;
    return 1.0;
}

CostEstimate WorkflowCostEstimator::estimatePipelineCost( const WorkflowDocument &def,
                                                          const QMap<QString, QSize> &rasterDimensions )
{
    return estimatePipelineCostWithHostRam( def, rasterDimensions, hostTotalRamBytes() );
}

CostEstimate WorkflowCostEstimator::estimatePipelineCostWithHostRam( const WorkflowDocument &def,
                                                                     const QMap<QString, QSize> &rasterDimensions,
                                                                     qint64 hostRamBytes )
{
    CostEstimate estimate;

    const auto tierInfo = WorkflowDagAnalyzer::computeConcurrencyTiers( def );

    double totalFlops = 0.0;
    for ( const NodeFact &node : def.nodes )
    {
        const QSize size = rasterSizeFor( node.nodeId, rasterDimensions );
        const int bands = node.outputPorts.isEmpty()
            ? 1
            : std::max( 1, node.outputPorts.first().bandCount );
        totalFlops += double( qint64( size.width() ) * qint64( size.height() ) * qint64( bands ) )
                      * operatorComplexity( node.operatorId );
    }
    estimate.totalFlops = totalFlops;
    estimate.estimatedDurationSeconds = totalFlops / kFlopsPerSecond;

    const qint64 peakWorking = tierWorkingSetBytes( tierInfo, def, rasterDimensions );
    estimate.peakRssBytes = peakWorking + kBaseEngineOverheadBytes;

    const int tierWidth = WorkflowDagAnalyzer::calculateMaxParallelism( tierInfo );
    const double waterlineBytes = double( hostRamBytes ) * kRssWaterline;
    if ( double( estimate.peakRssBytes ) > waterlineBytes )
        estimate.recommendedMaxParallelism = 1;
    else
        estimate.recommendedMaxParallelism = std::max( 1, std::min( tierWidth, kDefaultMaxParallelism ) );

    return estimate;
}

} // namespace sicnu::workflow
