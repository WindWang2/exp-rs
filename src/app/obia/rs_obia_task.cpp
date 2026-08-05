// rs_obia_task.cpp — Phase 10B Task 10B.4
#include "rs_obia_task.h"

#include "rs_class_raster.h"
#include "rs_object_classify.h"
#include "rs_obia_segmentation.h"

#include <gdal.h>

#include <QElapsedTimer>

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
        segCfg.threshold = mCfg.threshold;

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

    // Step 3: Build feature matrix (rows = segments)
    QVector<quint32> segmentIds;
    cv::Mat allFeatures = RsSegmentFeatures::toFeatureMatrix( stats, segmentIds, mCfg.featureSelection );

    if ( isCanceled() )
        return false;

    setProgress( 60 );

    // Steps 4–5: train-row selection + fit-if-needed + predict all segments,
    // owned by RsObjectClassify (ADR 0054).
    const RsObjectClassifyResult clsResult = RsObjectClassify::classify(
        allFeatures, segmentIds, mCfg.segmentLabels, *mCfg.backend );
    if ( !clsResult.ok )
    {
        mResult.ok = false;
        mResult.errorMessage = ( clsResult.labeledCount == 0 )
            ? tr( "No labeled segments for training" )
            : tr( "Classification failed: %1" ).arg( clsResult.errorMessage );
        return false;
    }

    mResult.labeledSegments = clsResult.labeledCount;
    const QMap<quint32, int> segmentClasses = clsResult.segmentClasses;
    mResult.segmentClasses = segmentClasses;
    mResult.segmentUncertainties = clsResult.segmentUncertainties;

    // Training-set accuracy (true labels vs predicted class of labeled segments).
    {
        QVector<int> yTrue;
        QVector<int> yPred;
        yTrue.reserve( mCfg.segmentLabels.size() );
        yPred.reserve( mCfg.segmentLabels.size() );
        for ( auto it = mCfg.segmentLabels.constBegin(); it != mCfg.segmentLabels.constEnd(); ++it )
        {
            const auto predIt = segmentClasses.constFind( it.key() );
            if ( predIt == segmentClasses.constEnd() )
                continue;
            yTrue.append( it.value() );
            yPred.append( predIt.value() );
        }
        mResult.accuracy = RsAccuracyAssessment::compute( yTrue, yPred );
    }

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
    // ADR 0054: the deep module owns class-raster painting (dtype escalation,
    // palette, georef copy, NoData=0, incomplete-output cleanup).
    const RsClassRasterResult result = RsClassRaster::paint(
        mSegMap, segmentClasses, mCfg.sourceRaster, mCfg.outputRaster, mCfg.classColors );
    if ( !result.ok )
    {
        errorMsg = result.errorMessage;
        return false;
    }

    mResult.totalPixels = result.totalPixels;
    return true;
}
