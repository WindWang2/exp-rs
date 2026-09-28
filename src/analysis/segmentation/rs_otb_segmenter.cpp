// rs_otb_segmenter.cpp — OTB CLI adapter (MeanShift + Watershed, raster mode).
#include "rs_otb_segmenter.h"

#include "sicnu_logging.h"
#include "tools/tool_path_manager.h"

#include <gdal.h>

#include <QFile>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <vector>

bool RsOtbSegmenter::isAvailable()
{
    return !ToolPathManager::instance().otbToolPath( QStringLiteral( "Segmentation" ) ).isEmpty();
}

bool RsOtbSegmenter::relabelAllVoidSegments( RsSegmentMap &segMap, const QString &rasterPath )
{
    if ( segMap.isEmpty() || rasterPath.isEmpty() )
        return false;

    GDALDatasetH srcDs = GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( !srcDs )
        return false;
    const int width = GDALGetRasterXSize( srcDs );
    const int height = GDALGetRasterYSize( srcDs );
    const int bandCount = GDALGetRasterCount( srcDs );
    if ( width != segMap.width() || height != segMap.height() || bandCount < 1 )
    {
        GDALClose( srcDs );
        return false;
    }

    // Per-band declared finite sentinels; bands without one void only on
    // non-finite samples (the rs:obia_features any-band invalid convention).
    std::vector<float> sentinels( static_cast<size_t>( bandCount ), 0.0f );
    std::vector<bool> hasSentinel( static_cast<size_t>( bandCount ), false );
    for ( int b = 1; b <= bandCount; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( srcDs, b );
        if ( !band )
        {
            GDALClose( srcDs );
            return false;
        }
        int hasNd = 0;
        const double nd = GDALGetRasterNoDataValue( band, &hasNd );
        if ( hasNd && std::isfinite( nd ) )
        {
            sentinels[static_cast<size_t>( b - 1 )] = static_cast<float>( nd );
            hasSentinel[static_cast<size_t>( b - 1 )] = true;
        }
    }

    const size_t nPixels = static_cast<size_t>( width ) * static_cast<size_t>( height );
    const QVector<quint32> &labels = segMap.labels();

    // 1 byte/px void mask (row-block reads, #648 convention): a pixel is void
    // when ANY band hits its declared sentinel or is non-finite.
    std::vector<char> pixelVoid( nPixels, 0 );
    {
        constexpr int kBlockRows = 256;
        std::vector<float> rowBlock;
        for ( int y = 0; y < height; y += kBlockRows )
        {
            const int rows = std::min( kBlockRows, height - y );
            for ( int b = 1; b <= bandCount; ++b )
            {
                GDALRasterBandH band = GDALGetRasterBand( srcDs, b );
                rowBlock.assign( static_cast<size_t>( width ) * rows, 0.0f );
                if ( GDALRasterIO( band, GF_Read, 0, y, width, rows, rowBlock.data(),
                                   width, rows, GDT_Float32, 0, 0 ) != CE_None )
                {
                    GDALClose( srcDs );
                    return false;
                }
                const bool bHasSentinel = hasSentinel[static_cast<size_t>( b - 1 )];
                const float sentinel = sentinels[static_cast<size_t>( b - 1 )];
                for ( int r = 0; r < rows; ++r )
                {
                    const float *row = rowBlock.data() + static_cast<size_t>( r ) * width;
                    char *voidRow = pixelVoid.data()
                                    + static_cast<size_t>( y + r ) * width;
                    for ( int x = 0; x < width; ++x )
                    {
                        const float v = row[x];
                        if ( !std::isfinite( v ) || ( bHasSentinel && v == sentinel ) )
                            voidRow[x] = 1;
                    }
                }
            }
        }
    }
    GDALClose( srcDs );

    // A label survives when it owns at least one non-void pixel; all-void
    // segments (fake objects clustered from sentinel regions) relabel to 0.
    QHash<quint32, bool> alive;
    alive.reserve( 1024 );
    for ( size_t i = 0; i < nPixels; ++i )
    {
        const quint32 label = labels[static_cast<qsizetype>( i )];
        if ( label != 0 && !pixelVoid[i] )
            alive[label] = true;
    }
    QVector<quint32> relabeled = labels;
    bool changed = false;
    for ( size_t i = 0; i < nPixels; ++i )
    {
        const quint32 label = relabeled[static_cast<qsizetype>( i )];
        if ( label != 0 && !alive.contains( label ) )
        {
            relabeled[static_cast<qsizetype>( i )] = 0;
            changed = true;
        }
    }
    if ( changed )
        segMap = RsSegmentMap( std::move( relabeled ), segMap.width(), segMap.height() );
    return true;
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

    // Void contract: declared-sentinel regions must not survive as fake
    // objects (R5 NoData audit). Best-effort — a failed relabel pass is
    // logged, it does not discard a successful segmentation.
    if ( !relabelAllVoidSegments( result.segMap, rasterPath ) )
    {
        SICNU_LOG_WARN( SicnuLogTags::Segmentation,
                           QStringLiteral( "RsOtbSegmenter: void relabel skipped "
                                           "(input unreadable or grid mismatch): %1" )
                             .arg( rasterPath ) );
    }

    result.ok = true;
    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation,
                       QStringLiteral( "RsOtbSegmenter complete: %1 segments" )
                         .arg( result.segMap.segmentCount() ) );
    return result;
}
