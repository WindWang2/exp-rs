// rs_hierarchy_features.cpp — F2a feature matrix.
#include "rs_hierarchy_features.h"

#include "sicnu_logging.h"

RsFeatureMatrixResult RsHierarchyFeatures::buildFeatureMatrix(
    const RsObjectHierarchy &hierarchy,
    const QString &rasterPath,
    int levelIndex,
    const QVector<int> &bandIndices )
{
    RsFeatureMatrixResult result;

    if ( hierarchy.isEmpty() )
    {
        result.errorMessage = QStringLiteral( "buildFeatureMatrix: empty hierarchy" );
        return result;
    }
    if ( levelIndex < 0 || levelIndex >= hierarchy.levelCount() )
    {
        result.errorMessage = QStringLiteral( "buildFeatureMatrix: level %1 out of range (0..%2)" )
                                .arg( levelIndex )
                                .arg( hierarchy.levelCount() - 1 );
        return result;
    }

    const RsSegmentMap &segMap = hierarchy.level( levelIndex );
    if ( segMap.isEmpty() )
    {
        result.errorMessage = QStringLiteral( "buildFeatureMatrix: empty segment map at level %1" )
                                .arg( levelIndex );
        return result;
    }

    const auto stats = RsSegmentFeatures::extract( rasterPath, segMap, bandIndices );
    if ( stats.isEmpty() )
    {
        result.errorMessage = QStringLiteral( "buildFeatureMatrix: feature extraction failed" );
        return result;
    }

#ifdef SICNU_HAS_OPENCV
    QVector<quint32> segmentIds;
    cv::Mat flatX = RsSegmentFeatures::toFeatureMatrix( stats, segmentIds );
    if ( flatX.empty() )
    {
        result.errorMessage = QStringLiteral( "buildFeatureMatrix: empty flat feature matrix" );
        return result;
    }

    return appendInterLevelFeatures( flatX, segmentIds, hierarchy, levelIndex );
#else
    Q_UNUSED( stats );
    result.errorMessage = QStringLiteral( "buildFeatureMatrix requires OpenCV" );
    return result;
#endif
}

#ifdef SICNU_HAS_OPENCV
RsFeatureMatrixResult RsHierarchyFeatures::appendInterLevelFeatures(
    const cv::Mat &flatX,
    const QVector<quint32> &segmentIds,
    const RsObjectHierarchy &hierarchy,
    int levelIndex )
{
    RsFeatureMatrixResult result;

    if ( flatX.empty() || segmentIds.isEmpty() || flatX.rows != segmentIds.size() )
    {
        result.errorMessage = QStringLiteral( "appendInterLevelFeatures: size mismatch" );
        return result;
    }

    const int baseCols = flatX.cols;
    const int nRows = flatX.rows;
    // +2: childCount, areaRatioToParent
    cv::Mat X( nRows, baseCols + 2, CV_32F );

    result.meta.segmentIds = segmentIds;
    result.meta.parentIds.resize( nRows );
    result.spectralShapeCols = baseCols;

    for ( int r = 0; r < nRows; ++r )
    {
        flatX.row( r ).copyTo( X.row( r ).colRange( 0, baseCols ) );

        const quint32 segId = segmentIds[r];
        const quint32 parentId = hierarchy.parentOf( levelIndex, segId );
        result.meta.parentIds[r] = parentId;

        const float childCount = static_cast<float>( hierarchy.childCount( levelIndex, segId ) );
        const float areaRatio = static_cast<float>( hierarchy.areaRatioToParent( levelIndex, segId ) );

        X.at<float>( r, baseCols ) = childCount;
        X.at<float>( r, baseCols + 1 ) = areaRatio;
    }

    result.X = X;
    result.ok = true;
    SICNU_LOG_INFO( SicnuLogTags::Segmentation,
                    QStringLiteral( "F2a matrix: %1 rows × %2 cols (level %3)" )
                      .arg( nRows )
                      .arg( X.cols )
                      .arg( levelIndex ) );
    return result;
}
#endif
