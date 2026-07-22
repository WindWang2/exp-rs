// rs_object_classify.cpp — Object-level train/predict on feature rows.
#include "rs_object_classify.h"

#ifdef SICNU_HAS_OPENCV

RsObjectClassifyResult RsObjectClassify::classify(
    const cv::Mat &X,
    const QVector<quint32> &segmentIds,
    const QMap<quint32, int> &trainingLabels,
    RsClassifierBackend &backend )
{
    RsObjectClassifyResult result;

    if ( X.empty() || segmentIds.isEmpty() || X.rows != segmentIds.size() )
    {
        result.errorMessage = QStringLiteral( "classify: empty or mismatched feature matrix" );
        return result;
    }

    QMap<quint32, int> segIdToRow;
    for ( int i = 0; i < segmentIds.size(); ++i )
        segIdToRow[segmentIds[i]] = i;

    std::vector<int> trainRows;
    std::vector<int> trainYvals;
    for ( auto it = trainingLabels.constBegin(); it != trainingLabels.constEnd(); ++it )
    {
        auto rowIt = segIdToRow.constFind( it.key() );
        if ( rowIt == segIdToRow.constEnd() )
            continue;
        trainRows.push_back( rowIt.value() );
        trainYvals.push_back( it.value() );
    }

    result.labeledCount = static_cast<int>( trainRows.size() );
    if ( trainRows.empty() )
    {
        result.errorMessage = QStringLiteral( "classify: no labeled segments in feature matrix" );
        return result;
    }

    const int nFeatures = X.cols;
    cv::Mat trainX( static_cast<int>( trainRows.size() ), nFeatures, CV_32F );
    cv::Mat trainY( static_cast<int>( trainRows.size() ), 1, CV_32S );
    for ( int i = 0; i < static_cast<int>( trainRows.size() ); ++i )
    {
        X.row( trainRows[static_cast<size_t>( i )] ).copyTo( trainX.row( i ) );
        trainY.at<int>( i, 0 ) = trainYvals[static_cast<size_t>( i )];
    }

    if ( !backend.isFitted() )
    {
        if ( !backend.fit( trainX, trainY ) )
        {
            result.errorMessage = QStringLiteral( "classify: backend training failed" );
            return result;
        }
    }

    cv::Mat predictions = backend.predict( X );
    if ( predictions.empty() || predictions.rows != X.rows )
    {
        result.errorMessage = QStringLiteral( "classify: prediction failed" );
        return result;
    }

    for ( int i = 0; i < segmentIds.size(); ++i )
        result.segmentClasses[segmentIds[i]] = predictions.at<int>( i, 0 );

    result.predictedCount = segmentIds.size();
    result.ok = true;
    return result;
}

#endif // SICNU_HAS_OPENCV
