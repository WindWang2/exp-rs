// evaluation.cpp — metric implementations (single-sourced formulas).
#include "evaluation.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace sicnu::experiment
{

namespace
{

double safeDivide( double numerator, double denominator )
{
    return denominator > 0.0 ? numerator / denominator : 0.0;
}

} // namespace

// --- ConfusionMatrix -------------------------------------------------------------

qint64 ConfusionMatrix::total() const
{
    qint64 sum = 0;
    for ( const qint64 value : m_counts )
        sum += value;
    return sum;
}

qint64 ConfusionMatrix::truthTotal( qint64 row ) const
{
    qint64 sum = 0;
    for ( qint64 column = 0; column < size(); ++column )
        sum += count( row, column );
    return sum;
}

qint64 ConfusionMatrix::predictedTotal( qint64 column ) const
{
    qint64 sum = 0;
    for ( qint64 row = 0; row < size(); ++row )
        sum += count( row, column );
    return sum;
}

double ConfusionMatrix::overallAccuracy() const
{
    const qint64 grand = total();
    qint64 diagonal = 0;
    for ( qint64 i = 0; i < size(); ++i )
        diagonal += truePositives( i );
    return safeDivide( double( diagonal ), double( grand ) );
}

double ConfusionMatrix::balancedAccuracy() const
{
    qint64 classesWithSupport = 0;
    double recallSum = 0.0;
    for ( qint64 i = 0; i < size(); ++i )
    {
        if ( truthTotal( i ) == 0 )
            continue;
        ++classesWithSupport;
        recallSum += safeDivide( double( truePositives( i ) ), double( truthTotal( i ) ) );
    }
    return classesWithSupport > 0 ? recallSum / double( classesWithSupport ) : 0.0;
}

ConfusionMatrix::PerClassMetrics ConfusionMatrix::perClass( qint64 index ) const
{
    PerClassMetrics metrics;
    if ( index < 0 || index >= size() )
        return metrics;
    metrics.label = m_labels.at( int( index ) );
    const double tp = double( truePositives( index ) );
    const double predicted = double( predictedTotal( index ) );
    const double truth = double( truthTotal( index ) );
    metrics.support = truthTotal( index );
    metrics.precision = safeDivide( tp, predicted );
    metrics.recall = safeDivide( tp, truth );
    metrics.f1 = metrics.precision + metrics.recall > 0.0
                     ? 2.0 * metrics.precision * metrics.recall /
                           ( metrics.precision + metrics.recall )
                     : 0.0;
    metrics.iou = safeDivide( tp, predicted + truth - tp );
    return metrics;
}

QVector<ConfusionMatrix::PerClassMetrics> ConfusionMatrix::perClassAll() const
{
    QVector<PerClassMetrics> all;
    for ( qint64 i = 0; i < size(); ++i )
        all.append( perClass( i ) );
    return all;
}

double ConfusionMatrix::macroPrecision() const
{
    double sum = 0.0;
    qint64 classes = 0;
    for ( qint64 i = 0; i < size(); ++i )
    {
        if ( predictedTotal( i ) == 0 )
            continue;
        sum += perClass( i ).precision;
        ++classes;
    }
    return classes > 0 ? sum / double( classes ) : 0.0;
}

double ConfusionMatrix::macroRecall() const
{
    double sum = 0.0;
    qint64 classes = 0;
    for ( qint64 i = 0; i < size(); ++i )
    {
        if ( truthTotal( i ) == 0 )
            continue;
        sum += perClass( i ).recall;
        ++classes;
    }
    return classes > 0 ? sum / double( classes ) : 0.0;
}

double ConfusionMatrix::macroF1() const
{
    double sum = 0.0;
    qint64 classes = 0;
    for ( qint64 i = 0; i < size(); ++i )
    {
        if ( truthTotal( i ) == 0 && predictedTotal( i ) == 0 )
            continue;
        sum += perClass( i ).f1;
        ++classes;
    }
    return classes > 0 ? sum / double( classes ) : 0.0;
}

double ConfusionMatrix::macroIoU() const
{
    double sum = 0.0;
    qint64 classes = 0;
    for ( qint64 i = 0; i < size(); ++i )
    {
        if ( truthTotal( i ) == 0 && predictedTotal( i ) == 0 )
            continue;
        sum += perClass( i ).iou;
        ++classes;
    }
    return classes > 0 ? sum / double( classes ) : 0.0;
}

double ConfusionMatrix::weightedF1() const
{
    const qint64 grand = total();
    if ( grand == 0 )
        return 0.0;
    double weighted = 0.0;
    for ( qint64 i = 0; i < size(); ++i )
        weighted += double( truthTotal( i ) ) * perClass( i ).f1;
    return weighted / double( grand );
}

double ConfusionMatrix::kappa() const
{
    const double grand = double( total() );
    if ( grand <= 0.0 )
        return 0.0;
    const double observed = overallAccuracy();
    double expected = 0.0;
    for ( qint64 i = 0; i < size(); ++i )
        expected += double( truthTotal( i ) ) * double( predictedTotal( i ) );
    expected /= grand * grand;
    return expected < 1.0 ? ( observed - expected ) / ( 1.0 - expected ) : 0.0;
}

double ConfusionMatrix::mcc() const
{
    // Gorodkin's multivariate R_K: covariance formulation over the k×k
    // matrix. Empty matrices report 0, never NaN.
    const double s = double( total() );
    if ( s <= 0.0 )
        return 0.0;
    double c = 0.0;
    double sumP = 0.0;
    double sumT = 0.0;
    double sumP2 = 0.0;
    double sumT2 = 0.0;
    double sumPT = 0.0;
    for ( qint64 k = 0; k < size(); ++k )
    {
        const double t = double( truthTotal( k ) );
        const double p = double( predictedTotal( k ) );
        c += truePositives( k );
        sumP += p;
        sumT += t;
        sumP2 += p * p;
        sumT2 += t * t;
        sumPT += p * t;
    }
    const double covariance = c * s - sumPT;
    const double denominator =
        std::sqrt( ( s * s - sumP2 ) * ( s * s - sumT2 ) );
    return denominator > 0.0 ? covariance / denominator : 0.0;
}

QJsonObject ConfusionMatrix::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( m_labels ) );
    QJsonArray rows;
    for ( qint64 row = 0; row < size(); ++row )
    {
        QJsonArray cells;
        for ( qint64 column = 0; column < size(); ++column )
            cells.append( count( row, column ) );
        rows.append( cells );
    }
    json.insert( QStringLiteral( "counts" ), rows );
    json.insert( QStringLiteral( "overall_accuracy" ), overallAccuracy() );
    json.insert( QStringLiteral( "balanced_accuracy" ), balancedAccuracy() );
    json.insert( QStringLiteral( "kappa" ), kappa() );
    json.insert( QStringLiteral( "mcc" ), mcc() );
    json.insert( QStringLiteral( "macro_f1" ), macroF1() );
    json.insert( QStringLiteral( "macro_iou" ), macroIoU() );
    json.insert( QStringLiteral( "weighted_f1" ), weightedF1() );
    QJsonArray perClassArray;
    for ( const PerClassMetrics &metrics : perClassAll() )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "label" ), metrics.label );
        item.insert( QStringLiteral( "precision" ), metrics.precision );
        item.insert( QStringLiteral( "recall" ), metrics.recall );
        item.insert( QStringLiteral( "f1" ), metrics.f1 );
        item.insert( QStringLiteral( "iou" ), metrics.iou );
        item.insert( QStringLiteral( "support" ), qint64( metrics.support ) );
        perClassArray.append( item );
    }
    json.insert( QStringLiteral( "per_class" ), perClassArray );
    return json;
}

Result<ConfusionMatrix> ConfusionMatrix::fromJson( const QJsonObject &json )
{
    using ResultT = Result<ConfusionMatrix>;
    ConfusionMatrix matrix;
    matrix.m_labels = json.value( QStringLiteral( "labels" ) ).toVariant().toStringList();
    if ( matrix.m_labels.isEmpty() )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "confusion matrix needs labels" ),
                                             DiagnosticSeverity::Error } );
    }
    matrix.m_counts = QVector<qint64>( int( matrix.size() * matrix.size() ), 0 );
    const QJsonArray rows = json.value( QStringLiteral( "counts" ) ).toArray();
    if ( rows.size() != int( matrix.size() ) )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "counts rows != label count" ),
                                             DiagnosticSeverity::Error } );
    }
    for ( int row = 0; row < rows.size(); ++row )
    {
        const QJsonArray cells = rows.at( row ).toArray();
        if ( cells.size() != int( matrix.size() ) )
        {
            return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                                 QStringLiteral( "counts row width mismatch" ),
                                                 DiagnosticSeverity::Error } );
        }
        for ( int column = 0; column < cells.size(); ++column )
        {
            const qint64 value = cells.at( column ).toInteger();
            if ( value < 0 )
            {
                return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                                     QStringLiteral( "negative cell count" ),
                                                     DiagnosticSeverity::Error } );
            }
            matrix.setCount( row, column, value );
        }
    }
    return ResultT::success( matrix );
}

// --- Regression ---------------------------------------------------------------------

Result<RegressionMetrics> regressionMetrics( const QVector<double> &truth,
                                             const QVector<double> &predicted )
{
    using ResultT = Result<RegressionMetrics>;
    if ( truth.size() != predicted.size() || truth.isEmpty() )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "truth/prediction length mismatch" ),
                                             DiagnosticSeverity::Error } );
    }
    RegressionMetrics metrics;
    metrics.count = truth.size();
    double absoluteSum = 0.0;
    double squaredSum = 0.0;
    double signedSum = 0.0;
    double percentageSum = 0.0;
    qint64 mapeCount = 0;
    for ( int i = 0; i < truth.size(); ++i )
    {
        const double difference = predicted.at( i ) - truth.at( i );
        absoluteSum += std::fabs( difference );
        squaredSum += difference * difference;
        signedSum += difference;
        if ( truth.at( i ) != 0.0 )
        {
            percentageSum += std::fabs( difference / truth.at( i ) );
            ++mapeCount;
        }
        else
        {
            ++metrics.mapeSkippedZeros;
        }
    }
    metrics.mae = absoluteSum / double( metrics.count );
    metrics.mse = squaredSum / double( metrics.count );
    metrics.rmse = std::sqrt( metrics.mse );
    metrics.bias = signedSum / double( metrics.count );
    metrics.mape = mapeCount > 0 ? 100.0 * percentageSum / double( mapeCount ) : 0.0;

    // R² = 1 - SSE/SST; a constant-truth series has no variance → 0 by
    // policy (not NaN, not ±inf).
    double mean = 0.0;
    for ( const double value : truth )
        mean += value;
    mean /= double( metrics.count );
    double sst = 0.0;
    for ( const double value : truth )
        sst += ( value - mean ) * ( value - mean );
    metrics.r2 = sst > 0.0 ? 1.0 - squaredSum / sst : 0.0;
    return ResultT::success( metrics );
}

QJsonObject RegressionMetrics::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "mae" ), mae );
    json.insert( QStringLiteral( "mse" ), mse );
    json.insert( QStringLiteral( "rmse" ), rmse );
    json.insert( QStringLiteral( "r2" ), r2 );
    json.insert( QStringLiteral( "bias" ), bias );
    json.insert( QStringLiteral( "mape" ), mape );
    json.insert( QStringLiteral( "mape_skipped_zeros" ), mapeSkippedZeros );
    json.insert( QStringLiteral( "count" ), count );
    return json;
}

// --- Boundary F-score ------------------------------------------------------------------

Result<double> boundaryFScore( const QVector<qint64> &truthBoundary,
                               const QVector<qint64> &predictedBoundary, qint64 imageWidth,
                               double tolerancePx )
{
    using ResultT = Result<double>;
    if ( imageWidth <= 0 || tolerancePx < 0.0 )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "bad boundary inputs" ),
                                             DiagnosticSeverity::Error } );
    }
    if ( truthBoundary.isEmpty() && predictedBoundary.isEmpty() )
        return ResultT::success( 1.0 ); // both empty = identical (no boundary)
    if ( truthBoundary.isEmpty() || predictedBoundary.isEmpty() )
        return ResultT::success( 0.0 );

    const int radius = int( std::ceil( tolerancePx ) );
    const qint64 cellSize = qMax<qint64>( 1, radius );
    QHash<qint64, QVector<qint64>> predictedCells;
    for ( const qint64 index : predictedBoundary )
    {
        const qint64 x = index % imageWidth;
        const qint64 y = index / imageWidth;
        predictedCells[( y / cellSize ) * 1000003 + ( x / cellSize )].append( index );
    }
    auto nearMatch = [&]( qint64 index ) {
        const qint64 x = index % imageWidth;
        const qint64 y = index / imageWidth;
        const qint64 cx = x / cellSize;
        const qint64 cy = y / cellSize;
        const double tolerance2 = tolerancePx * tolerancePx;
        for ( qint64 dy = -1; dy <= 1; ++dy )
        {
            for ( qint64 dx = -1; dx <= 1; ++dx )
            {
                const auto it = predictedCells.constFind( ( cy + dy ) * 1000003 + ( cx + dx ) );
                if ( it == predictedCells.constEnd() )
                    continue;
                for ( const qint64 candidate : it.value() )
                {
                    const double px = double( candidate % imageWidth );
                    const double py = double( candidate / imageWidth );
                    const double dist2 = ( px - x ) * ( px - x ) + ( py - y ) * ( py - y );
                    if ( dist2 <= tolerance2 )
                        return true;
                }
            }
        }
        return false;
    };

    qint64 truthMatched = 0;
    for ( const qint64 index : truthBoundary )
    {
        if ( nearMatch( index ) )
            ++truthMatched;
    }
    // Precision needs the REVERSE direction: how many PREDICTED boundary
    // pixels lie near some truth pixel. Counting truth matches only would
    // reward spraying predictions.
    const qint64 predictedCellSize = qMax<qint64>( 1, radius );
    QHash<qint64, QVector<qint64>> truthCells;
    for ( const qint64 index : truthBoundary )
    {
        const qint64 x = index % imageWidth;
        const qint64 y = index / imageWidth;
        truthCells[( y / predictedCellSize ) * 1000003 + ( x / predictedCellSize )]
            .append( index );
    }
    auto nearTruth = [&]( qint64 index ) {
        const qint64 x = index % imageWidth;
        const qint64 y = index / imageWidth;
        const qint64 cx = x / cellSize;
        const qint64 cy = y / cellSize;
        const double tolerance2 = tolerancePx * tolerancePx;
        for ( qint64 dy = -1; dy <= 1; ++dy )
        {
            for ( qint64 dx = -1; dx <= 1; ++dx )
            {
                const auto it = truthCells.constFind( ( cy + dy ) * 1000003 + ( cx + dx ) );
                if ( it == truthCells.constEnd() )
                    continue;
                for ( const qint64 candidate : it.value() )
                {
                    const double px = double( candidate % imageWidth );
                    const double py = double( candidate / imageWidth );
                    const double dist2 = ( px - x ) * ( px - x ) + ( py - y ) * ( py - y );
                    if ( dist2 <= tolerance2 )
                        return true;
                }
            }
        }
        return false;
    };
    qint64 predictedMatched = 0;
    for ( const qint64 index : predictedBoundary )
    {
        if ( nearTruth( index ) )
            ++predictedMatched;
    }
    const double precision = safeDivide( double( predictedMatched ),
                                         double( predictedBoundary.size() ) );
    const double recall = safeDivide( double( truthMatched ), double( truthBoundary.size() ) );
    return ResultT::success( precision + recall > 0.0
                                 ? 2.0 * precision * recall / ( precision + recall )
                                 : 0.0 );
}

// --- Detection AP ------------------------------------------------------------------------

double DetectionBox::iouWith( const DetectionBox &other ) const
{
    const double intersectionWidth =
        qMin( x + width, other.x + other.width ) - qMax( x, other.x );
    const double intersectionHeight =
        qMin( y + height, other.y + other.height ) - qMax( y, other.y );
    if ( intersectionWidth <= 0.0 || intersectionHeight <= 0.0 )
        return 0.0;
    const double intersection = intersectionWidth * intersectionHeight;
    const double unionArea = width * height + other.width * other.height - intersection;
    return unionArea > 0.0 ? intersection / unionArea : 0.0;
}

double averagePrecision( const QVector<DetectionBox> &sortedDetections,
                         qint64 groundTruthCount )
{
    if ( groundTruthCount <= 0 || sortedDetections.isEmpty() )
        return 0.0;
    qint64 truePositives = 0;
    double precisionSum = 0.0;
    int rank = 0;
    int index = 0;
    const int total = sortedDetections.size();
    while ( index < total )
    {
        // Confidence ties share ONE operating point: the whole tie block is
        // evaluated at its LAST rank, so an arbitrary input order inside the
        // block cannot swing the score.
        const double confidence = sortedDetections.at( index ).confidence;
        const int blockStart = index;
        qint64 blockTruePositives = 0;
        while ( index < total && sortedDetections.at( index ).confidence == confidence )
        {
            if ( sortedDetections.at( index ).isTruePositive )
                ++blockTruePositives;
            ++index;
        }
        rank += index - blockStart;
        truePositives += blockTruePositives;
        if ( blockTruePositives > 0 )
        {
            const double blockPrecision =
                double( truePositives ) / double( rank );
            precisionSum += blockPrecision * double( blockTruePositives );
        }
    }
    return double( precisionSum ) / double( groundTruthCount );
}

double meanAveragePrecision( const QVector<QVector<DetectionBox>> &perClassDetections,
                             const QVector<qint64> &perClassTruthCounts )
{
    if ( perClassDetections.size() != perClassTruthCounts.size() ||
         perClassDetections.isEmpty() )
        return 0.0;
    double sum = 0.0;
    for ( int i = 0; i < perClassDetections.size(); ++i )
    {
        QVector<DetectionBox> sorted = perClassDetections.at( i );
        std::sort( sorted.begin(), sorted.end(),
                   []( const DetectionBox &a, const DetectionBox &b ) {
                       if ( a.confidence != b.confidence )
                           return a.confidence > b.confidence;
                       return a.x < b.x; // deterministic tie-break
                   } );
        sum += averagePrecision( sorted, perClassTruthCounts.at( i ) );
    }
    return sum / double( perClassDetections.size() );
}

// --- EvaluationProtocol ---------------------------------------------------------------------

Result<void> EvaluationProtocol::validate() const
{
    using ResultT = Result<void>;
    auto fail = []( const QString &message ) {
        return ResultT::failure(
            Diagnostic{ QStringLiteral( "evaluation.protocol_invalid" ), message,
                        DiagnosticSeverity::Error } );
    };
    if ( m_datasetVersionId.isEmpty() || m_splitManifestId.isEmpty() )
        return fail( QStringLiteral( "protocol requires dataset version + split manifest" ) );
    if ( m_subset.isEmpty() )
        return fail( QStringLiteral( "protocol requires a subset" ) );
    if ( m_iouThreshold <= 0.0 || m_iouThreshold > 1.0 )
        return fail( QStringLiteral( "IoU threshold must be in (0,1]" ) );
    if ( m_confidenceThreshold < 0.0 || m_confidenceThreshold >= 1.0 )
        return fail( QStringLiteral( "confidence threshold must be in [0,1)" ) );
    if ( m_aggregation != QLatin1String( "macro" ) &&
         m_aggregation != QLatin1String( "micro" ) &&
         m_aggregation != QLatin1String( "weighted" ) )
        return fail( QStringLiteral( "aggregation must be macro|micro|weighted" ) );
    return ResultT::success();
}

QJsonObject EvaluationProtocol::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    json.insert( QStringLiteral( "split_manifest_id" ), m_splitManifestId );
    json.insert( QStringLiteral( "subset" ), m_subset );
    if ( !m_ignoreLabels.isEmpty() )
        json.insert( QStringLiteral( "ignore_labels" ),
                        QJsonArray::fromStringList( m_ignoreLabels ) );
    if ( !m_maskRef.isEmpty() )
        json.insert( QStringLiteral( "mask_ref" ), m_maskRef );
    json.insert( QStringLiteral( "iou_threshold" ), m_iouThreshold );
    json.insert( QStringLiteral( "confidence_threshold" ), m_confidenceThreshold );
    json.insert( QStringLiteral( "aggregation" ), m_aggregation );
    return json;
}

Result<EvaluationProtocol> EvaluationProtocol::fromJson( const QJsonObject &json )
{
    using ResultT = Result<EvaluationProtocol>;
    EvaluationProtocol protocol;
    protocol.m_datasetVersionId =
        json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    protocol.m_splitManifestId =
        json.value( QStringLiteral( "split_manifest_id" ) ).toString();
    protocol.m_subset =
        json.value( QStringLiteral( "subset" ) ).toString( QStringLiteral( "test" ) );
    protocol.m_ignoreLabels =
        json.value( QStringLiteral( "ignore_labels" ) ).toVariant().toStringList();
    protocol.m_maskRef = json.value( QStringLiteral( "mask_ref" ) ).toString();
    protocol.m_iouThreshold = json.value( QStringLiteral( "iou_threshold" ) ).toDouble( 0.5 );
    protocol.m_confidenceThreshold =
        json.value( QStringLiteral( "confidence_threshold" ) ).toDouble( 0.0 );
    protocol.m_aggregation =
        json.value( QStringLiteral( "aggregation" ) ).toString( QStringLiteral( "macro" ) );
    const auto validated = protocol.validate();
    if ( !validated )
        return ResultT::failure( validated.diagnostics() );
    return ResultT::success( protocol );
}

QJsonObject MetricRecord::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "run_id" ), runId );
    json.insert( QStringLiteral( "protocol" ), protocol.toJson() );
    json.insert( QStringLiteral( "metrics" ), metrics );
    json.insert( QStringLiteral( "metrics_hash" ), metricsHash );
    // M4 metrics schema versioning: readers refuse foreign layouts instead
    // of silently reinterpreting documents (missing key = v1-before-versioning).
    json.insert( QStringLiteral( "metrics_schema_version" ), metricsSchemaVersion );
    return json;
}

Result<MetricRecord> MetricRecord::fromJson( const QJsonObject &json )
{
    using ResultT = Result<MetricRecord>;
    MetricRecord record;
    record.runId = json.value( QStringLiteral( "run_id" ) ).toString();
    const auto protocol =
        EvaluationProtocol::fromJson( json.value( QStringLiteral( "protocol" ) ).toObject() );
    if ( !protocol )
        return ResultT::failure( protocol.diagnostics() );
    record.protocol = protocol.value();
    record.metrics = json.value( QStringLiteral( "metrics" ) ).toObject();
    record.metricsHash = json.value( QStringLiteral( "metrics_hash" ) ).toString();
    record.metricsSchemaVersion =
        json.value( QStringLiteral( "metrics_schema_version" ) ).toInteger( 1 );
    if ( record.runId.isEmpty() )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "metric record needs run id" ),
                                             DiagnosticSeverity::Error } );
    }
    return ResultT::success( record );
}

} // namespace sicnu::experiment
