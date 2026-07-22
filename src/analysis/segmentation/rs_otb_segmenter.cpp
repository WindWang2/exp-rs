// rs_otb_segmenter.cpp — OTB CLI adapter (MeanShift + Watershed, raster mode).
#include "rs_otb_segmenter.h"

#include "sicnu_logging.h"
#include "tools/tool_path_manager.h"

#include <gdal.h>

#include <QFile>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>

bool RsOtbSegmenter::isAvailable()
{
    return !ToolPathManager::instance().otbToolPath( QStringLiteral( "Segmentation" ) ).isEmpty();
}

RsSegmenterResult RsOtbSegmenter::segment(
    const QString &rasterPath,
    const RsLevelSpec &spec,
    const std::function<bool()> &isCanceled )
{
    RsSegmenterResult result;

    if ( rasterPath.isEmpty() || !QFile::exists( rasterPath ) )
    {
        result.errorMessage = QObject::tr( "OTB segmenter: input raster missing: %1" ).arg( rasterPath );
        return result;
    }

    const QString program = ToolPathManager::instance().otbToolPath( QStringLiteral( "Segmentation" ) );
    if ( program.isEmpty() )
    {
        result.errorMessage = QObject::tr(
            "OTB Segmentation CLI not found — set SICNU_OTB_PATH or install OTB. "
            "Hierarchical OBIA primary segmenters require OTB (no silent teaching fallback)." );
        return result;
    }

    QTemporaryDir tempDir;
    if ( !tempDir.isValid() )
    {
        result.errorMessage = QObject::tr( "Cannot create temporary directory for OTB output" );
        return result;
    }

    const QString labelOut = tempDir.path() + QStringLiteral( "/labels.tif" );

    QStringList args;
    args << QStringLiteral( "-in" ) << rasterPath;
    args << QStringLiteral( "-mode" ) << QStringLiteral( "raster" );

    switch ( spec.filter )
    {
        case RsLevelSpec::Filter::MeanShift:
            args << QStringLiteral( "-filter" ) << QStringLiteral( "meanshift" );
            args << QStringLiteral( "-filter.meanshift.spatialr" ) << QString::number( spec.spatialRadius );
            args << QStringLiteral( "-filter.meanshift.ranger" ) << QString::number( spec.rangeRadius, 'f', 2 );
            args << QStringLiteral( "-filter.meanshift.minsize" ) << QString::number( spec.minRegionSize );
            args << QStringLiteral( "-filter.meanshift.maxiter" ) << QString::number( spec.maxIterations );
            args << QStringLiteral( "-filter.meanshift.thres" ) << QString::number( spec.threshold, 'f', 4 );
            break;
        case RsLevelSpec::Filter::Watershed:
            args << QStringLiteral( "-filter" ) << QStringLiteral( "watershed" );
            args << QStringLiteral( "-filter.watershed.threshold" )
                 << QString::number( spec.watershedThreshold, 'f', 4 );
            break;
    }

    args << QStringLiteral( "-mode.raster.out" ) << labelOut << QStringLiteral( "uint32" );

    const QString cmdLine = program + QLatin1Char( ' ' ) + args.join( QLatin1Char( ' ' ) );
    SICNU_LOG_INFO( SicnuLogTags::Segmentation,
                    QStringLiteral( "RsOtbSegmenter: %1" ).arg( cmdLine ) );

    QProcess proc;
    proc.setProcessChannelMode( QProcess::MergedChannels );
    proc.start( program, args );

    if ( !proc.waitForStarted( 5000 ) )
    {
        result.errorMessage = QObject::tr( "Failed to start OTB Segmentation: %1" ).arg( proc.errorString() );
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
            SICNU_LOG_INFO( SicnuLogTags::Segmentation, QString::fromUtf8( output ) );
    }

    proc.waitForFinished( -1 );

    if ( proc.exitCode() != 0 )
    {
        result.errorMessage = QObject::tr( "OTB Segmentation failed (exit %1): %2" )
                                .arg( proc.exitCode() )
                                .arg( QString::fromUtf8( proc.readAllStandardOutput() ) );
        return result;
    }

    if ( !QFile::exists( labelOut ) )
    {
        result.errorMessage = QObject::tr( "OTB did not produce label image: %1" ).arg( labelOut );
        return result;
    }

    result.segMap = RsSegmentMap::fromGeoTIFF( labelOut );
    if ( result.segMap.isEmpty() )
    {
        result.errorMessage = QObject::tr( "Failed to load OTB label image into memory" );
        return result;
    }

    // Optional size check against source
    GDALDatasetH srcDs = GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( srcDs )
    {
        const int srcW = GDALGetRasterXSize( srcDs );
        const int srcH = GDALGetRasterYSize( srcDs );
        GDALClose( srcDs );
        if ( result.segMap.width() != srcW || result.segMap.height() != srcH )
        {
            result.errorMessage = QObject::tr( "OTB label size mismatch: %1x%2 vs source %3x%4" )
                                    .arg( result.segMap.width() )
                                    .arg( result.segMap.height() )
                                    .arg( srcW )
                                    .arg( srcH );
            result.segMap = RsSegmentMap();
            return result;
        }
    }

    result.ok = true;
    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation,
                       QStringLiteral( "RsOtbSegmenter complete: %1 segments" )
                         .arg( result.segMap.segmentCount() ) );
    return result;
}
