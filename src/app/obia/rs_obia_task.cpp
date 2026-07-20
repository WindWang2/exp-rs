// rs_obia_task.cpp — Phase 10B Task 10B.4
#include "rs_obia_task.h"

#include "rs_obia_segmentation.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_error.h>
#include <cpl_conv.h>
#include <cpl_string.h>
#include <ogr_srs_api.h>

#include <QElapsedTimer>
#include <QFile>
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
        RsObiaSegmentationConfig segCfg;
        segCfg.rasterPath = mCfg.sourceRaster;
        segCfg.bandIndices = mCfg.bandIndices;
        segCfg.preferOtb = mCfg.useOtb;
        segCfg.spatialRadius = mCfg.spatialRadius;
        segCfg.rangeRadius = mCfg.rangeRadius;
        segCfg.minRegionSize = mCfg.minRegionSize;
        segCfg.maxIteration = mCfg.maxIteration;
        segCfg.smoothKernel = mCfg.smoothKernel;
        segCfg.quantizeBins = mCfg.quantizeBins;

        const RsObiaSegmentationResult segResult = RsObiaSegmentation::run(
            segCfg, [this]() { return isCanceled(); } );

        if ( !segResult.ok )
        {
            mResult.ok = false;
            mResult.errorMessage = tr( "Segmentation failed: %1" ).arg( segResult.errorMessage );
            return false;
        }

        mSegMap = segResult.segMap;
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

    // Choose output type: UInt16 when any class id exceeds 255, else Byte
    int maxClassId = 0;
    for ( auto it = segmentClasses.constBegin(); it != segmentClasses.constEnd(); ++it )
    {
        if ( it.value() < 0 )
        {
            errorMsg = tr( "Negative class ID %1 is not supported" ).arg( it.value() );
            GDALClose( srcDs );
            return false;
        }
        if ( it.value() > maxClassId )
            maxClassId = it.value();
    }
    for ( auto it = mCfg.classColors.constBegin(); it != mCfg.classColors.constEnd(); ++it )
    {
        if ( it.key() > maxClassId )
            maxClassId = it.key();
    }

    if ( maxClassId > 65535 )
    {
        errorMsg = tr( "Class ID %1 exceeds UInt16 range (0-65535)" ).arg( maxClassId );
        GDALClose( srcDs );
        return false;
    }

    const bool useUInt16 = maxClassId > 255;
    const GDALDataType outType = useUInt16 ? GDT_UInt16 : GDT_Byte;

    char **papszOptions = nullptr;
    papszOptions = CSLSetNameValue( papszOptions, "COMPRESS", "LZW" );
    GDALDatasetH dstDs = GDALCreate( driver, mCfg.outputRaster.toUtf8().constData(),
                                      w, h, 1, outType, papszOptions );
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

    GDALRasterBandH outBand = GDALGetRasterBand( dstDs, 1 );

    // Palette color tables are primarily for Byte; attach only when ids fit 0-255
    if ( !useUInt16 )
    {
        GDALColorTableH ct = GDALCreateColorTable( GPI_RGB );

        GDALColorEntry nodataColor = { 0, 0, 0, 0 };
        GDALSetColorEntry( ct, 0, &nodataColor );

        for ( auto it = mCfg.classColors.constBegin(); it != mCfg.classColors.constEnd(); ++it )
        {
            GDALColorEntry ce;
            ce.c1 = static_cast<short>( it.value().red() );
            ce.c2 = static_cast<short>( it.value().green() );
            ce.c3 = static_cast<short>( it.value().blue() );
            ce.c4 = 255;
            GDALSetColorEntry( ct, it.key(), &ce );
        }

        GDALSetRasterColorTable( outBand, ct );
        GDALSetRasterColorInterpretation( outBand, GCI_PaletteIndex );
        GDALDestroyColorTable( ct );
    }

    // Write pixel data: for each pixel, look up segment → class
    const auto &labels = mSegMap.labels();
    int totalPixels = 0;

    if ( useUInt16 )
    {
        QVector<quint16> rowBuf( w );
        for ( int r = 0; r < h; ++r )
        {
            for ( int c = 0; c < w; ++c )
            {
                quint32 segId = labels[r * w + c];
                if ( segId == 0 )
                {
                    rowBuf[c] = 0;
                }
                else
                {
                    auto it = segmentClasses.find( segId );
                    if ( it != segmentClasses.end() )
                    {
                        rowBuf[c] = static_cast<quint16>( it.value() );
                        totalPixels++;
                    }
                    else
                    {
                        rowBuf[c] = 0;
                    }
                }
            }
            if ( GDALRasterIO( outBand, GF_Write, 0, r, w, 1,
                               rowBuf.data(), w, 1, GDT_UInt16, 0, 0 ) != CE_None )
            {
                errorMsg = tr( "RasterIO write failed at row %1" ).arg( r );
                GDALClose( dstDs );
                return false;
            }
        }
    }
    else
    {
        QVector<quint8> rowBuf( w );
        for ( int r = 0; r < h; ++r )
        {
            for ( int c = 0; c < w; ++c )
            {
                quint32 segId = labels[r * w + c];
                if ( segId == 0 )
                {
                    rowBuf[c] = 0;
                }
                else
                {
                    auto it = segmentClasses.find( segId );
                    if ( it != segmentClasses.end() )
                    {
                        rowBuf[c] = static_cast<quint8>( it.value() );
                        totalPixels++;
                    }
                    else
                    {
                        rowBuf[c] = 0;
                    }
                }
            }
            if ( GDALRasterIO( outBand, GF_Write, 0, r, w, 1,
                               rowBuf.data(), w, 1, GDT_Byte, 0, 0 ) != CE_None )
            {
                errorMsg = tr( "RasterIO write failed at row %1" ).arg( r );
                GDALClose( dstDs );
                return false;
            }
        }
    }

    mResult.totalPixels = totalPixels;

    GDALClose( dstDs );
    return true;
}
