// rs_obia_segmentation.cpp — Shared OTB / SimpleSegmenter segmentation for OBIA.
#include "rs_obia_segmentation.h"

#include "rs_simple_segmenter.h"
#include "sicnu_logging.h"
#include "tools/tool_path_manager.h"

#include <gdal.h>
#include <cpl_error.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>

namespace
{

RsObiaSegmentationResult runOtb(
    const RsObiaSegmentationConfig &cfg,
    const std::function<bool()> &isCanceled )
{
    RsObiaSegmentationResult result;

    const QString program = ToolPathManager::instance().otbToolPath( QStringLiteral( "Segmentation" ) );
    if ( program.isEmpty() )
    {
        result.errorMessage = QObject::tr( "OTB Segmentation CLI not found — set SICNU_OTB_PATH or install OTB" );
        return result;
    }

    QTemporaryDir tempDir;
    if ( !tempDir.isValid() )
    {
        result.errorMessage = QObject::tr( "Cannot create temporary directory for OTB output" );
        return result;
    }

    const QString vectorOut = tempDir.path() + QStringLiteral( "/segments.shp" );
    const QString labelOut = tempDir.path() + QStringLiteral( "/labels.tif" );

    QStringList args;
    args << QStringLiteral( "-in" ) << cfg.rasterPath;
    args << QStringLiteral( "-mode" ) << QStringLiteral( "meanshift" );
    args << QStringLiteral( "-mode.meanshift.spatialr" ) << QString::number( cfg.spatialRadius );
    args << QStringLiteral( "-mode.meanshift.ranger" ) << QString::number( cfg.rangeRadius, 'f', 2 );
    args << QStringLiteral( "-mode.meanshift.minsize" ) << QString::number( cfg.minRegionSize );
    args << QStringLiteral( "-mode.meanshift.maxiter" ) << QString::number( cfg.maxIteration );
    args << QStringLiteral( "-out" ) << vectorOut << labelOut << QStringLiteral( "uint32" );

    const QString cmdLine = program + QLatin1Char( ' ' ) + args.join( QLatin1Char( ' ' ) );
    SICNU_LOG_INFO( SicnuLogTags::OBIA, QStringLiteral( "Running OTB Segmentation: %1" ).arg( cmdLine ) );

    QProcess proc;
    proc.setProcessChannelMode( QProcess::MergedChannels );
    proc.start( program, args );

    if ( !proc.waitForStarted( 5000 ) )
    {
        result.errorMessage = QObject::tr( "Failed to start OTB Segmentation: %1" ).arg( proc.errorString() );
        SICNU_LOG_ERROR( SicnuLogTags::OBIA, result.errorMessage );
        return result;
    }

    while ( proc.state() == QProcess::Running )
    {
        if ( isCanceled && isCanceled() )
        {
            proc.kill();
            proc.waitForFinished( 3000 );
            result.errorMessage = QObject::tr( "OTB segmentation canceled" );
            return result;
        }
        proc.waitForReadyRead( 100 );
        const QByteArray output = proc.readAllStandardOutput();
        if ( !output.isEmpty() )
            SICNU_LOG_INFO( SicnuLogTags::OBIA, QString::fromUtf8( output ) );
    }

    proc.waitForFinished( -1 );

    if ( proc.exitCode() != 0 )
    {
        result.errorMessage = QObject::tr( "OTB Segmentation failed (exit %1): %2" )
                                 .arg( proc.exitCode() )
                                 .arg( QString::fromUtf8( proc.readAllStandardOutput() ) );
        SICNU_LOG_ERROR( SicnuLogTags::OBIA, result.errorMessage );
        return result;
    }

    if ( !QFile::exists( labelOut ) )
    {
        result.errorMessage = QObject::tr( "OTB did not produce label image: %1" ).arg( labelOut );
        SICNU_LOG_ERROR( SicnuLogTags::OBIA, result.errorMessage );
        return result;
    }

    result.segMap = RsSegmentMap::fromGeoTIFF( labelOut );
    if ( result.segMap.isEmpty() )
    {
        result.errorMessage = QObject::tr( "Failed to load OTB label image" );
        return result;
    }

    GDALDatasetH srcDs = GDALOpen( cfg.rasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( srcDs )
    {
        const int srcW = GDALGetRasterXSize( srcDs );
        const int srcH = GDALGetRasterYSize( srcDs );
        GDALClose( srcDs );

        if ( result.segMap.width() != srcW || result.segMap.height() != srcH )
        {
            result.errorMessage = QObject::tr( "Label image size mismatch: %1x%2 vs source %3x%4" )
                                      .arg( result.segMap.width() )
                                      .arg( result.segMap.height() )
                                      .arg( srcW )
                                      .arg( srcH );
            result.segMap = RsSegmentMap();
            return result;
        }
    }

    result.ok = true;
    result.usedOtb = true;
    SICNU_LOG_SUCCESS( SicnuLogTags::OBIA,
                       QStringLiteral( "OTB segmentation complete: %1 segments" ).arg( result.segMap.segmentCount() ) );
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