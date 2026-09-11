// promotion.cpp — Model Evaluation / Promotion Seam (goal M8).
//
// Evaluates recorded evidence, never intentions: a criterion without a
// recorded metric number does not pass, and a run outside the benchmark
// set is a typed gap — not a waived requirement.
#include "promotion.h"

#include "experiment_ids.h"
#include "metric_path.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <set>

namespace sicnu::experiment
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

namespace
{

Diagnostic promotionError( const QString &message )
{
    return Diagnostic{ QStringLiteral( "experiment.promotion_invalid" ), message,
                       DiagnosticSeverity::Error };
}

} // namespace

QJsonObject PromotionCriterion::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "metric" ), metric );
    json.insert( QStringLiteral( "min_value" ), minValue );
    return json;
}

Result<PromotionCriterion> PromotionCriterion::fromJson( const QJsonObject &json )
{
    PromotionCriterion criterion;
    criterion.metric = json.value( QStringLiteral( "metric" ) ).toString();
    criterion.minValue = json.value( QStringLiteral( "min_value" ) ).toDouble();
    if ( criterion.metric.isEmpty() )
        return Result<PromotionCriterion>::failure(
            promotionError( QStringLiteral( "criterion requires a metric" ) ) );
    return Result<PromotionCriterion>::success( criterion );
}

QJsonObject PromotionRequest::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "run_id" ), runId );
    json.insert( QStringLiteral( "model_id" ), modelId );
    json.insert( QStringLiteral( "model_digest" ), modelDigest );
    QJsonArray criteriaArray;
    for ( const PromotionCriterion &criterion : criteria )
        criteriaArray.append( criterion.toJson() );
    json.insert( QStringLiteral( "criteria" ), criteriaArray );
    json.insert( QStringLiteral( "benchmark_dataset_versions" ),
                 QJsonArray::fromStringList(
                     QStringList( benchmarkDatasetVersions.cbegin(),
                                  benchmarkDatasetVersions.cend() ) ) );
    return json;
}

Result<PromotionRequest> PromotionRequest::fromJson( const QJsonObject &json )
{
    PromotionRequest request;
    request.runId = json.value( QStringLiteral( "run_id" ) ).toString();
    request.modelId = json.value( QStringLiteral( "model_id" ) ).toString();
    request.modelDigest = json.value( QStringLiteral( "model_digest" ) ).toString();
    for ( const QJsonValue &value :
          json.value( QStringLiteral( "criteria" ) ).toArray() )
    {
        const auto criterion = PromotionCriterion::fromJson( value.toObject() );
        if ( !criterion )
            return Result<PromotionRequest>::failure( criterion.diagnostics() );
        request.criteria.append( criterion.value() );
    }
    for ( const QJsonValue &value :
          json.value( QStringLiteral( "benchmark_dataset_versions" ) ).toArray() )
        request.benchmarkDatasetVersions.append( value.toString() );
    if ( request.runId.isEmpty() )
        return Result<PromotionRequest>::failure(
            promotionError( QStringLiteral( "promotion request requires run_id" ) ) );
    return Result<PromotionRequest>::success( request );
}

QJsonObject PromotionEvaluation::CriterionResult::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "metric" ), metric );
    json.insert( QStringLiteral( "value" ), value );
    json.insert( QStringLiteral( "passed" ), passed );
    if ( !detail.isEmpty() )
        json.insert( QStringLiteral( "detail" ), detail );
    return json;
}

QJsonObject PromotionEvaluation::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "eligible" ), eligible );
    QJsonArray array;
    for ( const CriterionResult &result : results )
        array.append( result.toJson() );
    json.insert( QStringLiteral( "results" ), array );
    json.insert( QStringLiteral( "missing_evidence" ),
                 QJsonArray::fromStringList( missingEvidence ) );
    return json;
}

PromotionEvaluator::PromotionEvaluator( ExperimentStore &store )
    : m_store( &store )
{
}

Result<PromotionEvaluation> PromotionEvaluator::evaluate(
    const PromotionRequest &request ) const
{
    if ( request.runId.isEmpty() )
        return Result<PromotionEvaluation>::failure(
            promotionError( QStringLiteral( "promotion request requires run_id" ) ) );

    PromotionEvaluation evaluation;

    const auto run = m_store->runById( request.runId );
    if ( !run )
    {
        evaluation.missingEvidence.append( QStringLiteral( "run" ) );
        return Result<PromotionEvaluation>::success( evaluation );
    }
    if ( run->status() != RunStatus::Completed )
    {
        // Only a completed run is promotion evidence — a failed/interrupted
        // candidate cannot be "grandfathered" in.
        evaluation.missingEvidence.append( QStringLiteral( "completed_status" ) );
    }
    if ( !request.modelDigest.isEmpty() && run->modelDigest().isEmpty() )
    {
        // The request names a digest the evidence run never pinned: the
        // evidence cannot vouch for THAT artifact.
        evaluation.missingEvidence.append( QStringLiteral( "model_digest_pin" ) );
    }
    else if ( !request.modelDigest.isEmpty() && run->modelDigest() != request.modelDigest )
    {
        evaluation.missingEvidence.append( QStringLiteral( "model_digest_mismatch" ) );
    }

    const auto record = m_store->metricRecordForRun( request.runId );
    if ( !record.has_value() )
        evaluation.missingEvidence.append( QStringLiteral( "metrics" ) );

    for ( const PromotionCriterion &criterion : request.criteria )
    {
        PromotionEvaluation::CriterionResult result;
        result.metric = criterion.metric;
        if ( record.has_value() )
        {
            const auto value = metricValueAtPath( record->metrics, criterion.metric );
            if ( value.has_value() )
            {
                result.value = *value;
                result.passed = *value >= criterion.minValue;
                if ( !result.passed )
                    result.detail = QStringLiteral( "%1 < %2" )
                                        .arg( *value )
                                        .arg( criterion.minValue );
            }
            else
            {
                result.passed = false;
                result.detail = QStringLiteral( "metric not recorded" );
                evaluation.missingEvidence.append(
                    QStringLiteral( "metric:%1" ).arg( criterion.metric ) );
            }
        }
        else
        {
            result.passed = false;
            result.detail = QStringLiteral( "no metric record" );
        }
        evaluation.results.append( result );
    }

    if ( !request.benchmarkDatasetVersions.isEmpty() )
    {
        const std::set<QString> benchmarks( request.benchmarkDatasetVersions.cbegin(),
                                            request.benchmarkDatasetVersions.cend() );
        if ( !benchmarks.contains( run->datasetVersionId() ) )
        {
            evaluation.missingEvidence.append( QStringLiteral( "benchmark_set" ) );
        }
    }

    evaluation.eligible =
        evaluation.missingEvidence.isEmpty() &&
        std::all_of( evaluation.results.cbegin(), evaluation.results.cend(),
                     []( const PromotionEvaluation::CriterionResult &result ) {
                         return result.passed;
                     } );
    return Result<PromotionEvaluation>::success( evaluation );
}

Result<QString> PromotionEvaluator::record( const PromotionRequest &request,
                                            const PromotionEvaluation &evaluation,
                                            const QString &decision,
                                            const QString &decidedBy ) const
{
    if ( decision != QStringLiteral( "pending" ) &&
         decision != QStringLiteral( "approved" ) && decision != QStringLiteral( "rejected" ) )
        return Result<QString>::failure( promotionError(
            QStringLiteral( "decision must be pending, approved or rejected" ) ) );

    PromotionRecord record;
    record.promotionId = ExperimentId::generate().toString();
    record.runId = request.runId;
    record.modelId = request.modelId;
    record.modelDigest = request.modelDigest;
    const auto run = m_store->runById( request.runId );
    record.datasetVersionId = run.has_value() ? run->datasetVersionId() : QString();
    record.verdict = evaluation.eligible ? QStringLiteral( "eligible" )
                                         : QStringLiteral( "ineligible" );
    record.decision = decision;
    record.decidedBy = decidedBy;
    record.criteriaJson = QString::fromUtf8(
        QJsonDocument( evaluation.toJson() ).toJson( QJsonDocument::Compact ) );
    record.createdAtUtc = QDateTime::currentDateTimeUtc();
    if ( decision != QStringLiteral( "pending" ) )
        record.decidedAtUtc = QDateTime::currentDateTimeUtc();

    const auto saved = m_store->savePromotionRecord( record );
    if ( !saved )
        return Result<QString>::failure( saved.diagnostics() );
    return Result<QString>::success( record.promotionId );
}

} // namespace sicnu::experiment
