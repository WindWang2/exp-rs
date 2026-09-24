// benchmark_runner.cpp — headless benchmark execution.
#include "benchmark_runner.h"

#include "../dataset/dataset_store.h"
#include "../dataset/split.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QUuid>

namespace sicnu::experiment
{

QString benchmarkRunStatusToString( BenchmarkRunStatus status )
{
    switch ( status )
    {
        case BenchmarkRunStatus::Completed:
            return QStringLiteral( "completed" );
        case BenchmarkRunStatus::Failed:
            return QStringLiteral( "failed" );
        case BenchmarkRunStatus::Aborted:
            return QStringLiteral( "aborted" );
    }
    return QStringLiteral( "failed" );
}

QJsonObject BenchmarkResult::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kBenchmarkResultSerializationVersion );
    json.insert( QStringLiteral( "result_id" ), m_resultId );
    json.insert( QStringLiteral( "benchmark_id" ), m_benchmarkId );
    json.insert( QStringLiteral( "benchmark_version" ), qint64( m_benchmarkVersion ) );
    json.insert( QStringLiteral( "definition_digest" ), m_definitionDigest );
    json.insert( QStringLiteral( "status" ), benchmarkRunStatusToString( m_status ) );
    if ( !m_failureCode.isEmpty() )
        json.insert( QStringLiteral( "failure_code" ), m_failureCode );
    if ( !m_failureMessage.isEmpty() )
        json.insert( QStringLiteral( "failure_message" ), m_failureMessage );
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    json.insert( QStringLiteral( "split_manifest_id" ), m_splitManifestId );
    if ( !m_modelId.isEmpty() )
        json.insert( QStringLiteral( "model_id" ), m_modelId );
    if ( !m_modelDigest.isEmpty() )
        json.insert( QStringLiteral( "model_digest" ), m_modelDigest );
    json.insert( QStringLiteral( "seed" ), qint64( m_seed ) );
    if ( !m_softwareRevision.isEmpty() )
        json.insert( QStringLiteral( "software_revision" ), m_softwareRevision );
    if ( !m_experimentRunId.isEmpty() )
        json.insert( QStringLiteral( "experiment_run_id" ), m_experimentRunId );
    json.insert( QStringLiteral( "reproducibility_complete" ), m_reproducibilityComplete );
    if ( !m_reproducibilityGaps.isEmpty() )
        json.insert( QStringLiteral( "reproducibility_gaps" ),
                     QJsonArray::fromStringList( m_reproducibilityGaps ) );
    QJsonArray metrics;
    for ( const MetricResult &metric : m_metrics )
        metrics.append( metric.toJson() );
    json.insert( QStringLiteral( "metrics" ), metrics );
    if ( !m_rawMetrics.isEmpty() )
        json.insert( QStringLiteral( "raw_metrics" ), m_rawMetrics );
    json.insert( QStringLiteral( "protocol" ), m_protocol.toJson() );
    return json;
}

namespace
{
BenchmarkRunStatus statusFromString( const QString &text )
{
    if ( text == QLatin1String( "completed" ) )
        return BenchmarkRunStatus::Completed;
    if ( text == QLatin1String( "aborted" ) )
        return BenchmarkRunStatus::Aborted;
    return BenchmarkRunStatus::Failed;
}
} // namespace

Result<BenchmarkResult> BenchmarkResult::fromJson( const QJsonObject &json )
{
    using ResultT = Result<BenchmarkResult>;
    if ( json.value( QStringLiteral( "schema_version" ) ).toInt() !=
         kBenchmarkResultSerializationVersion )
    {
        return ResultT::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_result_version" ),
            QStringLiteral( "unsupported benchmark result schema_version" ),
            DiagnosticSeverity::Error,
        } );
    }
    BenchmarkResult result;
    result.setResultId( json.value( QStringLiteral( "result_id" ) ).toString() );
    result.setBenchmarkId( json.value( QStringLiteral( "benchmark_id" ) ).toString() );
    // A negative stored version wraps through quint64 and defeats id/version
    // comparability — clamp like benchmark_definition.cpp does.
    result.setBenchmarkVersion( quint64( qMax<qint64>(
        0, json.value( QStringLiteral( "benchmark_version" ) ).toInteger( 0 ) ) ) );
    result.setDefinitionDigest( json.value( QStringLiteral( "definition_digest" ) ).toString() );
    result.setStatus( statusFromString( json.value( QStringLiteral( "status" ) ).toString() ) );
    result.setFailureCode( json.value( QStringLiteral( "failure_code" ) ).toString() );
    result.setFailureMessage( json.value( QStringLiteral( "failure_message" ) ).toString() );
    result.setDatasetVersionId( json.value( QStringLiteral( "dataset_version_id" ) ).toString() );
    result.setSplitManifestId( json.value( QStringLiteral( "split_manifest_id" ) ).toString() );
    result.setModelId( json.value( QStringLiteral( "model_id" ) ).toString() );
    result.setModelDigest( json.value( QStringLiteral( "model_digest" ) ).toString() );
    result.setSeed( quint64( json.value( QStringLiteral( "seed" ) ).toInteger( 0 ) ) );
    result.setSoftwareRevision( json.value( QStringLiteral( "software_revision" ) ).toString() );
    result.setExperimentRunId( json.value( QStringLiteral( "experiment_run_id" ) ).toString() );
    result.setReproducibilityComplete(
        json.value( QStringLiteral( "reproducibility_complete" ) ).toBool() );
    for ( const QJsonValue &gap : json.value( QStringLiteral( "reproducibility_gaps" ) ).toArray() )
        result.reproducibilityGaps().append( gap.toString() );
    for ( const QJsonValue &metric : json.value( QStringLiteral( "metrics" ) ).toArray() )
        result.metrics().append( MetricResult::fromJson( metric.toObject() ) );
    result.setRawMetrics( json.value( QStringLiteral( "raw_metrics" ) ).toObject() );
    // Protocol parse failure propagates like its siblings (MetricRecord,
    // BenchmarkDefinition): a torn or hostile protocol block must not come
    // back as a default-constructed protocol — that would slip fabricated
    // evaluation semantics (IoU 0.5 / subset "test" / macro) past the
    // store's fail-closed read gates into comparisons.
    const auto protocol =
        EvaluationProtocol::fromJson( json.value( QStringLiteral( "protocol" ) ).toObject() );
    if ( !protocol )
        return ResultT::failure( protocol.diagnostics() );
    result.setProtocol( protocol.value() );
    if ( result.resultId().isEmpty() || result.benchmarkId().isEmpty() )
    {
        return ResultT::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_result_invalid" ),
            QStringLiteral( "result_id and benchmark_id are required" ),
            DiagnosticSeverity::Error,
        } );
    }
    return ResultT::success( result );
}

QVector<MetricResult> metricResultsFromConfusion( const ConfusionMatrix &matrix,
                                                  const QStringList &metricNames,
                                                  const QString &scope )
{
    QVector<MetricResult> out;
    const QString defVer = QStringLiteral( "exp-rs.evaluation/1" );
    auto add = [&]( const QString &name, double value, qint64 support = -1,
                    const QString &classCode = QString() ) {
        if ( !metricNames.contains( name ) )
            return;
        MetricResult metric;
        metric.name = name;
        metric.definitionVersion = defVer;
        metric.value = value;
        metric.scope = classCode.isEmpty() ? scope : QStringLiteral( "class:%1" ).arg( classCode );
        metric.classCode = classCode;
        metric.support = support;
        out.append( metric );
    };

    add( QStringLiteral( "overall_accuracy" ), matrix.overallAccuracy(), matrix.total() );
    add( QStringLiteral( "oa" ), matrix.overallAccuracy(), matrix.total() );
    add( QStringLiteral( "kappa" ), matrix.kappa(), matrix.total() );
    add( QStringLiteral( "macro_f1" ), matrix.macroF1() );
    add( QStringLiteral( "macro_precision" ), matrix.macroPrecision() );
    add( QStringLiteral( "macro_recall" ), matrix.macroRecall() );
    add( QStringLiteral( "macro_iou" ), matrix.macroIoU() );
    add( QStringLiteral( "weighted_f1" ), matrix.weightedF1() );
    add( QStringLiteral( "mcc" ), matrix.mcc() );

    if ( metricNames.contains( QStringLiteral( "per_class_f1" ) ) ||
         metricNames.contains( QStringLiteral( "per_class_iou" ) ) )
    {
        for ( qint64 i = 0; i < matrix.size(); ++i )
        {
            const auto pc = matrix.perClass( i );
            if ( metricNames.contains( QStringLiteral( "per_class_f1" ) ) )
                add( QStringLiteral( "per_class_f1" ), pc.f1, pc.support, pc.label );
            if ( metricNames.contains( QStringLiteral( "per_class_iou" ) ) )
                add( QStringLiteral( "per_class_iou" ), pc.iou, pc.support, pc.label );
        }
    }
    return out;
}

namespace
{

bool isPseudoLike( sicnu::dataset::AnnotationSourceType source )
{
    using sicnu::dataset::AnnotationSourceType;
    return source == AnnotationSourceType::Pseudo || source == AnnotationSourceType::Weak ||
           source == AnnotationSourceType::ModelAssisted;
}

BenchmarkResult failResult( const BenchmarkDefinition &def, const QString &code,
                            const QString &message )
{
    BenchmarkResult result;
    result.setResultId( QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    result.setBenchmarkId( def.benchmarkId() );
    result.setBenchmarkVersion( def.benchmarkVersion() );
    result.setDefinitionDigest( def.contentDigest() );
    result.setStatus( BenchmarkRunStatus::Failed );
    result.setFailureCode( code );
    result.setFailureMessage( message );
    result.setDatasetVersionId( def.datasetVersionId() );
    result.setSplitManifestId( def.splitManifestId() );
    // The read gate refuses results whose protocol fails validation — a
    // Failed row carrying a default-constructed (pin-less) protocol would
    // poison its benchmark's entire stored history. A refusal run already
    // carries the completed, validated definition protocol it declined to
    // evaluate under; record it truthfully.
    result.setProtocol( def.protocol() );
    return result;
}

} // namespace

Result<BenchmarkResult> BenchmarkRunner::run( const BenchmarkRunRequest &request )
{
    BenchmarkDefinition def = request.definition;
    if ( def.protocol().datasetVersionId().isEmpty() )
        def.protocol().setDatasetVersionId( def.datasetVersionId() );
    if ( def.protocol().splitManifestId().isEmpty() )
        def.protocol().setSplitManifestId( def.splitManifestId() );

    const auto validated = def.validate();
    if ( !validated )
    {
        return Result<BenchmarkResult>::failure( validated.diagnostics() );
    }

    // Optional store pin checks.
    if ( request.store )
    {
        if ( !request.store->isOpen() )
        {
            return Result<BenchmarkResult>::failure( Diagnostic{
                QStringLiteral( "experiment.benchmark_store_closed" ),
                QStringLiteral( "dataset store is not open" ),
                DiagnosticSeverity::Error,
            } );
        }
        const auto versionId =
            sicnu::dataset::DatasetVersionId::fromString( def.datasetVersionId() );
        if ( !versionId )
        {
            return Result<BenchmarkResult>::success( failResult(
                def, QStringLiteral( "experiment.benchmark_bad_version" ),
                QStringLiteral( "dataset_version_id is not a valid id" ) ) );
        }
        const auto version = request.store->versionById( *versionId );
        if ( !version )
        {
            return Result<BenchmarkResult>::success( failResult(
                def, QStringLiteral( "experiment.benchmark_version_missing" ),
                QStringLiteral( "dataset version not found in store" ) ) );
        }
        if ( version->isMutable() )
        {
            return Result<BenchmarkResult>::success( failResult(
                def, QStringLiteral( "experiment.benchmark_version_not_frozen" ),
                QStringLiteral( "benchmark refuses mutable (draft) dataset versions" ) ) );
        }
        const auto split = request.store->splitManifestById( def.splitManifestId() );
        if ( !split )
        {
            return Result<BenchmarkResult>::success( failResult(
                def, QStringLiteral( "experiment.benchmark_split_missing" ),
                QStringLiteral( "split manifest not found in store" ) ) );
        }
        if ( split->datasetVersionId() != def.datasetVersionId() )
        {
            return Result<BenchmarkResult>::success( failResult(
                def, QStringLiteral( "experiment.benchmark_split_mismatch" ),
                QStringLiteral( "split manifest belongs to a different dataset version" ) ) );
        }
    }

    // Pseudo-label gate for protected test subsets.
    if ( def.refusePseudoLabelsInTest() &&
         ( def.protocol().subset() == QLatin1String( "test" ) ||
           def.protocol().subset().startsWith( QLatin1String( "fold:" ) ) ) )
    {
        for ( const BenchmarkTruth &truth : request.truths )
        {
            if ( isPseudoLike( truth.labelSource ) )
            {
                return Result<BenchmarkResult>::success( failResult(
                    def, QStringLiteral( "experiment.benchmark_pseudo_in_test" ),
                    QStringLiteral(
                        "pseudo/weak/model-assisted label in protected test subset (sample %1)" )
                        .arg( truth.sampleId ) ) );
            }
        }
    }

    if ( request.truths.isEmpty() || request.predictions.isEmpty() )
    {
        return Result<BenchmarkResult>::success( failResult(
            def, QStringLiteral( "experiment.benchmark_empty_inputs" ),
            QStringLiteral( "truths and predictions are required" ) ) );
    }

    // Classification path: build confusion matrix from matched sample ids.
    QHash<QString, QString> truthById;
    QHash<QString, sicnu::dataset::AnnotationSourceType> sourceById;
    for ( const BenchmarkTruth &truth : request.truths )
    {
        truthById.insert( truth.sampleId, truth.truthClass );
        sourceById.insert( truth.sampleId, truth.labelSource );
    }

    QStringList labels;
    QSet<QString> labelSet;
    for ( const BenchmarkTruth &truth : request.truths )
    {
        if ( !truth.truthClass.isEmpty() && !labelSet.contains( truth.truthClass ) )
        {
            labelSet.insert( truth.truthClass );
            labels.append( truth.truthClass );
        }
    }
    for ( const BenchmarkPrediction &pred : request.predictions )
    {
        if ( !pred.predictedClass.isEmpty() && !labelSet.contains( pred.predictedClass ) )
        {
            labelSet.insert( pred.predictedClass );
            labels.append( pred.predictedClass );
        }
    }
    labels.sort();

    ConfusionMatrix matrix( labels, labels.size() );
    QHash<QString, int> labelIndex;
    for ( int i = 0; i < labels.size(); ++i )
        labelIndex.insert( labels.at( i ), i );

    qint64 matched = 0;
    for ( const BenchmarkPrediction &pred : request.predictions )
    {
        const auto it = truthById.constFind( pred.sampleId );
        if ( it == truthById.constEnd() )
            continue;
        const int t = labelIndex.value( *it, -1 );
        const int p = labelIndex.value( pred.predictedClass, -1 );
        if ( t < 0 || p < 0 )
            continue;
        // Honor ignore labels from protocol.
        if ( def.protocol().ignoreLabels().contains( *it ) )
            continue;
        matrix.increment( t, p );
        ++matched;
    }

    if ( matched == 0 )
    {
        return Result<BenchmarkResult>::success( failResult(
            def, QStringLiteral( "experiment.benchmark_no_overlap" ),
            QStringLiteral( "no overlapping truth/prediction sample ids" ) ) );
    }

    BenchmarkResult result;
    result.setResultId( QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    result.setBenchmarkId( def.benchmarkId() );
    result.setBenchmarkVersion( def.benchmarkVersion() );
    result.setDefinitionDigest( def.contentDigest() );
    result.setStatus( BenchmarkRunStatus::Completed );
    result.setDatasetVersionId( def.datasetVersionId() );
    result.setSplitManifestId( def.splitManifestId() );
    result.setModelId( request.modelId );
    result.setModelDigest( request.modelDigest );
    result.setSeed( request.seed );
    result.setSoftwareRevision( request.softwareRevision );
    result.setExperimentRunId( request.experimentRunId );
    result.setProtocol( def.protocol() );
    result.setRawMetrics( matrix.toJson() );
    result.metrics() =
        metricResultsFromConfusion( matrix, def.metricNames(),
                                    QStringLiteral( "subset:%1" ).arg( def.protocol().subset() ) );

    // Honest reproducibility gaps — never fabricate a score.
    QStringList gaps;
    if ( request.modelDigest.isEmpty() )
        gaps.append( QStringLiteral( "model_digest" ) );
    if ( request.softwareRevision.isEmpty() )
        gaps.append( QStringLiteral( "software_revision" ) );
    for ( const QString &pin : def.requiredEnvironmentPins() )
    {
        if ( pin == QLatin1String( "provider" ) && request.provider.isEmpty() )
            gaps.append( pin );
        else if ( pin == QLatin1String( "device" ) && request.device.isEmpty() )
            gaps.append( pin );
        else if ( pin == QLatin1String( "software_revision" ) &&
                  request.softwareRevision.isEmpty() &&
                  !gaps.contains( QStringLiteral( "software_revision" ) ) )
            gaps.append( pin );
    }
    result.reproducibilityGaps() = gaps;
    result.setReproducibilityComplete( gaps.isEmpty() );

    return Result<BenchmarkResult>::success( result );
}

} // namespace sicnu::experiment
