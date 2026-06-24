// rs_obia_task.cpp — Phase 10B Task 10B.4
#include "rs_obia_task.h"

#include "rs_simple_segmenter.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_error.h>
#include <cpl_conv.h>
#include <cpl_string.h>
#include <ogr_srs_api.h>

#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RsObiaTask::RsObiaTask( Config cfg )
    : QgsTask( tr( "OBIA Classification" ), QgsTask::CanCancel )
    , mCfg( std::move( cfg ) )
{
}

void RsObiaTask::cancel()
{
    QgsTask::cancel();
}

// ---------------------------------------------------------------------------
// Main pipeline
// ---------------------------------------------------------------------------

bool RsObiaTask::run()
{
    // ISSUE 9 fix: ensure GDAL drivers registered on this thread
    static bool s_gdalInit = ( GDALAllRegister(), true );
    Q_UNUSED( s_gdalInit );

    QElapsedTimer timer;
    timer.start();

    // MEDIUM #10 fix: validate backend is non-null
    if ( !mCfg.backend )
    {
        mResult.errorMessage = tr( "No classifier backend configured" );
        return false;
    }

    setProgress( 5 );

    // Step 1: Segmentation (skip if pre-computed segment map provided)
    QMap<quint32, RsSegmentFeatures::SegmentStat> stats;
    if ( !mCfg.existingSegMap.isEmpty() )
    {
        // Reuse existing segment map from main window
        mSegMap = mCfg.existingSegMap;
    }
    else
    {
        QString segError;
        bool segOk = false;
        if ( mCfg.useOtb )
            segOk = runOtbSegmentation( segError );
        if ( !segOk )
            segOk = runSimpleSegmentation( segError );

        if ( !segOk )
        {
            mResult.ok = false;
            mResult.errorMessage = tr( "Segmentation failed: %1" ).arg( segError );
            return false;
        }
    }

    if ( isCanceled() )
        return false;

    setProgress( 30 );

    // Step 2: Extract features (skip if pre-computed stats provided)
    if ( !mCfg.existingStats.isEmpty() )
    {
        stats = mCfg.existingStats;
    }
    else
    {
        stats = RsSegmentFeatures::extract( mCfg.sourceRaster, mSegMap, mCfg.bandIndices );
        if ( stats.isEmpty() )
        {
            mResult.ok = false;
            mResult.errorMessage = tr( "Feature extraction failed: no segments found" );
            return false;
        }
    }

    mResult.totalSegments = stats.size();

    if ( isCanceled() )
        return false;

    setProgress( 50 );

    // Step 3: Build training set from labeled segments
    QVector<quint32> segmentIds;
    cv::Mat allFeatures = RsSegmentFeatures::toFeatureMatrix( stats, segmentIds );

    // Map segment ID → row index
    QMap<quint32, int> segIdToRow;
    for ( int i = 0; i < segmentIds.size(); ++i )
        segIdToRow[segmentIds[i]] = i;

    // Collect training rows
    std::vector<int> trainRows;
    std::vector<int> trainLabels;
    for ( auto it = mCfg.segmentLabels.constBegin(); it != mCfg.segmentLabels.constEnd(); ++it )
    {
        auto rowIt = segIdToRow.find( it.key() );
        if ( rowIt != segIdToRow.end() )
        {
            trainRows.push_back( rowIt.value() );
            trainLabels.push_back( it.value() );
        }
    }

    mResult.labeledSegments = static_cast<int>( trainRows.size() );

    if ( trainRows.empty() )
    {
        mResult.ok = false;
        mResult.errorMessage = tr( "No labeled segments for training" );
        return false;
    }

    // Build cv::Mat training data
    const int nFeatures = allFeatures.cols;
    cv::Mat trainX( static_cast<int>( trainRows.size() ), nFeatures, CV_32F );
    cv::Mat trainY( static_cast<int>( trainRows.size() ), 1, CV_32S );
    for ( int i = 0; i < static_cast<int>( trainRows.size() ); ++i )
    {
        allFeatures.row( trainRows[i] ).copyTo( trainX.row( i ) );
        trainY.at<int>( i, 0 ) = trainLabels[i];
    }

    if ( isCanceled() )
        return false;

    setProgress( 60 );

    // Step 4: Train classifier
    if ( !mCfg.backend->isFitted() )
    {
        if ( !mCfg.backend->fit( trainX, trainY ) )
        {
            mResult.ok = false;
            mResult.errorMessage = tr( "Classifier training failed" );
            return false;
        }
    }

    setProgress( 70 );

    // Step 5: Predict all segments
    cv::Mat predictions = mCfg.backend->predict( allFeatures );

    // Build segment → class mapping
    QMap<quint32, int> segmentClasses;
    for ( int i = 0; i < segmentIds.size(); ++i )
        segmentClasses[segmentIds[i]] = predictions.at<int>( i, 0 );

    if ( isCanceled() )
        return false;

    setProgress( 85 );

    // Step 6: Write output
    QString writeError;
    if ( !writeOutput( segmentClasses, writeError ) )
    {
        mResult.ok = false;
        mResult.errorMessage = tr( "Output write failed: %1" ).arg( writeError );
        return false;
    }

    mResult.ok = true;
    mResult.durationMs = static_cast<int>( timer.elapsed() );
    setProgress( 100 );
    return true;
}

// ---------------------------------------------------------------------------
// Segmentation: OTB MeanShift
// ---------------------------------------------------------------------------

bool RsObiaTask::runOtbSegmentation( QString &errorMsg )
{
    // Check if OTB CLI is available
    // We construct the command manually since we don't have direct access
    // to ToolPathManager from the analysis layer.
    // The caller should set useOtb=false if OTB is not available.

    // For now, fall through to SimpleSegmenter
    errorMsg = tr( "OTB segmentation not directly available in task; using fallback" );
    return false;
}

// ---------------------------------------------------------------------------
// Segmentation: SimpleSegmenter fallback
// ---------------------------------------------------------------------------

bool RsObiaTask::runSimpleSegmentation( QString &errorMsg )
{
    GDALDatasetH ds = GDALOpen( mCfg.sourceRaster.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
    {
        errorMsg = tr( "Cannot open raster: %1" ).arg( mCfg.sourceRaster );
        return false;
    }

    const int w = GDALGetRasterXSize( ds );
    const int h = GDALGetRasterYSize( ds );
    const int nBands = mCfg.bandIndices.size();

    if ( nBands == 0 )
    {
        GDALClose( ds );
        errorMsg = tr( "No bands selected" );
        return false;
    }

    // Read band data
    QVector<QVector<float>> bandData( nBands );
    for ( int b = 0; b < nBands; ++b )
    {
        bandData[b].resize( w * h );
        GDALRasterBandH band = GDALGetRasterBand( ds, mCfg.bandIndices[b] );
        if ( !band )
        {
            GDALClose( ds );
            errorMsg = tr( "Cannot read band %1" ).arg( mCfg.bandIndices[b] );
            return false;
        }
        if ( GDALRasterIO( band, GF_Read, 0, 0, w, h,
                           bandData[b].data(), w, h, GDT_Float32, 0, 0 ) != CE_None )
        {
            GDALClose( ds );
            errorMsg = tr( "RasterIO failed for band %1" ).arg( mCfg.bandIndices[b] );
            return false;
        }
    }

    // Get nodata value
    GDALRasterBandH firstBand = GDALGetRasterBand( ds, mCfg.bandIndices[0] );
    int hasNodata = 0;
    float nodata = static_cast<float>( GDALGetRasterNoDataValue( firstBand, &hasNodata ) );
    GDALClose( ds );

    if ( !hasNodata )
        nodata = -9999.0f;

    // Build band pointer array
    QVector<const float *> bandPtrs( nBands );
    for ( int b = 0; b < nBands; ++b )
        bandPtrs[b] = bandData[b].data();

    // Segment
    RsSimpleSegmenter::Params params;
    params.smoothKernel = mCfg.smoothKernel;
    params.quantizeBins = mCfg.quantizeBins;
    params.minRegionSize = mCfg.minRegionSize;

    mSegMap = RsSimpleSegmenter::segmentMultiBand( bandPtrs.constData(), nBands, w, h, nodata, params );

    if ( mSegMap.isEmpty() )
    {
        errorMsg = tr( "Segmentation produced empty result" );
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Write output GeoTIFF
// ---------------------------------------------------------------------------

bool RsObiaTask::writeOutput( const QMap<quint32, int> &segmentClasses, QString &errorMsg )
{
    // Open source to copy georeferencing
    GDALDatasetH srcDs = GDALOpen( mCfg.sourceRaster.toUtf8().constData(), GA_ReadOnly );
    if ( !srcDs )
    {
        errorMsg = tr( "Cannot reopen source raster" );
        return false;
    }

    const int w = GDALGetRasterXSize( srcDs );
    const int h = GDALGetRasterYSize( srcDs );

    // Create output
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
    {
        GDALClose( srcDs );
        errorMsg = tr( "GTiff driver not available" );
        return false;
    }

    char **papszOptions = nullptr;
    papszOptions = CSLSetNameValue( papszOptions, "COMPRESS", "LZW" );
    GDALDatasetH dstDs = GDALCreate( driver, mCfg.outputRaster.toUtf8().constData(),
                                      w, h, 1, GDT_Byte, papszOptions );
    CSLDestroy( papszOptions );

    if ( !dstDs )
    {
        GDALClose( srcDs );
        errorMsg = tr( "Cannot create output file: %1" ).arg( mCfg.outputRaster );
        return false;
    }

    // Copy georeferencing
    double geoTransform[6];
    if ( GDALGetGeoTransform( srcDs, geoTransform ) == CE_None )
        GDALSetGeoTransform( dstDs, geoTransform );
    const char *proj = GDALGetProjectionRef( srcDs );
    if ( proj && proj[0] )
        GDALSetProjection( dstDs, proj );

    GDALClose( srcDs );

    // Set up color table
    GDALColorTableH ct = GDALCreateColorTable( GPI_RGB );

    // Index 0 = nodata (transparent)
    GDALColorEntry nodataColor = { 0, 0, 0, 0 };
    GDALSetColorEntry( ct, 0, &nodataColor );

    // Class colors
    for ( auto it = mCfg.classColors.constBegin(); it != mCfg.classColors.constEnd(); ++it )
    {
        GDALColorEntry ce;
        ce.c1 = static_cast<short>( it.value().red() );
        ce.c2 = static_cast<short>( it.value().green() );
        ce.c3 = static_cast<short>( it.value().blue() );
        ce.c4 = 255;
        GDALSetColorEntry( ct, it.key(), &ce );
    }

    GDALRasterBandH outBand = GDALGetRasterBand( dstDs, 1 );
    GDALSetRasterColorTable( outBand, ct );
    GDALSetRasterColorInterpretation( outBand, GCI_PaletteIndex );
    GDALDestroyColorTable( ct );

    // HIGH #6 fix: validate class IDs fit in byte range
    for ( auto it = segmentClasses.constBegin(); it != segmentClasses.constEnd(); ++it )
    {
        if ( it.value() < 0 || it.value() > 255 )
        {
            errorMsg = tr( "Class ID %1 exceeds byte range (0-255)" ).arg( it.value() );
            GDALClose( dstDs );
            return false;
        }
    }

    // Write pixel data: for each pixel, look up segment → class
    QVector<quint8> rowBuf( w );
    const auto &labels = mSegMap.labels();
    int totalPixels = 0;

    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            quint32 segId = labels[r * w + c];
            if ( segId == 0 )
                rowBuf[c] = 0; // nodata
            else
            {
                auto it = segmentClasses.find( segId );
                if ( it != segmentClasses.end() )
                {
                    rowBuf[c] = static_cast<quint8>( it.value() );
                    totalPixels++;
                }
                else
                    rowBuf[c] = 0; // unclassified
            }
        }
        // HIGH #3 fix: check write result
        if ( GDALRasterIO( outBand, GF_Write, 0, r, w, 1,
                           rowBuf.data(), w, 1, GDT_Byte, 0, 0 ) != CE_None )
        {
            errorMsg = tr( "RasterIO write failed at row %1" ).arg( r );
            GDALClose( dstDs );
            return false;
        }
    }

    mResult.totalPixels = totalPixels;

    GDALClose( dstDs );
    return true;
}
