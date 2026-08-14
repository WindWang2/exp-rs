// rs_obia_segmentation.cpp — Shared OTB / SimpleSegmenter segmentation for OBIA.
#include "rs_obia_segmentation.h"

#include "analysis/segmentation/rs_otb_segmenter.h"
#include "rs_simple_segmenter.h"
#include "sicnu_logging.h"
#include "tools/tool_path_manager.h"

#include <gdal.h>

#include <QObject>

namespace
{

RsObiaSegmentationResult runOtb(
    const RsObiaSegmentationConfig &cfg,
    const std::function<bool()> &isCanceled )
{
    // Delegate to the analysis-layer OTB adapter (ADR 0058): one OTB CLI
    // dialect for all OBIA paths. The old app dialect (-mode meanshift with a
    // discarded shp output) is dropped for -mode raster, which produces the
    // label image directly and validates its size against the source.
    RsLevelSpec spec;
    spec.filter = RsLevelSpec::Filter::MeanShift;
    spec.spatialRadius = cfg.spatialRadius;
    spec.rangeRadius = cfg.rangeRadius;
    spec.minRegionSize = cfg.minRegionSize;
    spec.maxIterations = cfg.maxIteration;

    RsSegmenterResult otb = RsOtbSegmenter{}.segment( cfg.rasterPath, spec, isCanceled );

    RsObiaSegmentationResult result;
    result.ok = otb.ok;
    result.errorMessage = otb.errorMessage;
    result.segMap = otb.segMap;
    result.usedOtb = otb.ok; // only meaningful when ok; run() discards failures
    return result;
}

RsObiaSegmentationResult runSimple( const RsObiaSegmentationConfig &cfg )
{
    RsObiaSegmentationResult result;

    GDALDatasetH ds = GDALOpen( cfg.rasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
    {
        result.errorMessage = QObject::tr( "Cannot open raster: %1" ).arg( cfg.rasterPath );
        return result;
    }

    const int w = GDALGetRasterXSize( ds );
    const int h = GDALGetRasterYSize( ds );
    const int nBands = cfg.bandIndices.size();

    if ( nBands == 0 )
    {
        GDALClose( ds );
        result.errorMessage = QObject::tr( "No bands selected" );
        return result;
    }

    QVector<QVector<float>> bandData( nBands );
    for ( int b = 0; b < nBands; ++b )
    {
        bandData[b].resize( w * h );
        GDALRasterBandH band = GDALGetRasterBand( ds, cfg.bandIndices[b] );
        if ( !band )
        {
            GDALClose( ds );
            result.errorMessage = QObject::tr( "Cannot read band %1" ).arg( cfg.bandIndices[b] );
            return result;
        }
        if ( GDALRasterIO( band, GF_Read, 0, 0, w, h,
                           bandData[b].data(), w, h, GDT_Float32, 0, 0 ) != CE_None )
        {
            GDALClose( ds );
            result.errorMessage = QObject::tr( "RasterIO failed for band %1" ).arg( cfg.bandIndices[b] );
            return result;
        }
    }

    GDALRasterBandH firstBand = GDALGetRasterBand( ds, cfg.bandIndices[0] );
    int hasNodata = 0;
    float nodata = static_cast<float>( GDALGetRasterNoDataValue( firstBand, &hasNodata ) );
    GDALClose( ds );

    if ( !hasNodata )
        nodata = -9999.0f;

    QVector<const float *> bandPtrs( nBands );
    for ( int b = 0; b < nBands; ++b )
        bandPtrs[b] = bandData[b].data();

    RsSimpleSegmenter::Params params;
    params.smoothKernel = cfg.smoothKernel;
    params.quantizeBins = cfg.quantizeBins;
    params.minRegionSize = cfg.minRegionSize;

    result.segMap = RsSimpleSegmenter::segmentMultiBand( bandPtrs.constData(), nBands, w, h, nodata, params );
    if ( result.segMap.isEmpty() )
    {
        result.errorMessage = QObject::tr( "Segmentation produced empty result" );
        return result;
    }

    result.ok = true;
    result.usedOtb = false;
    SICNU_LOG_SUCCESS( SicnuLogTags::OBIA,
                       QStringLiteral( "Built-in segmentation complete: %1 segments" ).arg( result.segMap.segmentCount() ) );
    return result;
}

} // namespace

bool RsObiaSegmentation::isOtbAvailable()
{
    return !ToolPathManager::instance().otbToolPath( QStringLiteral( "Segmentation" ) ).isEmpty();
}

RsObiaSegmentationResult RsObiaSegmentation::run(
    const RsObiaSegmentationConfig &cfg,
    const std::function<bool()> &isCanceled )
{
    // Deliberate app-layer policy (ADR 0058): prefer OTB, fall back to the
    // teaching segmenter when OTB is missing or fails. This diverges from the
    // hierarchy path, where RsOtbSegmenter has a strict no-fallback rule —
    // the OBIA task must still produce an acceptable result on machines
    // without OTB.
    if ( cfg.preferOtb && isOtbAvailable() )
    {
        RsObiaSegmentationResult otbResult = runOtb( cfg, isCanceled );
        if ( otbResult.ok )
            return otbResult;

        SICNU_LOG_WARN( SicnuLogTags::OBIA,
                        QStringLiteral( "OTB segmentation failed, falling back to built-in segmenter: %1" )
                            .arg( otbResult.errorMessage ) );
    }

    return runSimple( cfg );
}