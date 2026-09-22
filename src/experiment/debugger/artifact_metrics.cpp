// artifact_metrics.cpp — bounded artifact/metric comparison (RS14-06, ADR 0174).
// Slice E GREEN implementation.

#include "artifact_metrics.h"

#include "../metric_path.h"

#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <functional>

namespace sicnu::experiment::debugger
{

namespace
{

QJsonObject artifactComparisonToJson( const ArtifactComparison &comparison )
{
    QJsonObject json;
    QString verdict;
    switch ( comparison.digestVerdict )
    {
        case ArtifactComparison::DigestVerdict::BothAbsent:
            verdict = QStringLiteral( "both_absent" );
            break;
        case ArtifactComparison::DigestVerdict::Equal:
            verdict = QStringLiteral( "equal" );
            break;
        case ArtifactComparison::DigestVerdict::Different:
            verdict = QStringLiteral( "different" );
            break;
        case ArtifactComparison::DigestVerdict::IncomparableModes:
            verdict = QStringLiteral( "incomparable_modes" );
            break;
        case ArtifactComparison::DigestVerdict::OneSided:
            verdict = QStringLiteral( "one_sided" );
            break;
    }
    json.insert( QLatin1String( "digest_verdict" ), verdict );
    json.insert( QLatin1String( "reference_step_id" ), comparison.referenceStepId );
    json.insert( QLatin1String( "student_step_id" ), comparison.studentStepId );
    json.insert( QLatin1String( "operator_id" ), comparison.operatorId );
    json.insert( QLatin1String( "reference_digest" ), comparison.referenceDigest );
    json.insert( QLatin1String( "reference_mode" ), comparison.referenceMode );
    json.insert( QLatin1String( "student_digest" ), comparison.studentDigest );
    json.insert( QLatin1String( "student_mode" ), comparison.studentMode );
    json.insert( QLatin1String( "sizes_comparable" ), comparison.sizesComparable );
    json.insert( QLatin1String( "reference_size_bytes" ),
                 static_cast<double>( comparison.referenceSizeBytes ) );
    json.insert( QLatin1String( "student_size_bytes" ),
                 static_cast<double>( comparison.studentSizeBytes ) );
    return json;
}

QJsonObject metricDeltaToJson( const MetricDeltaFinding &finding )
{
    QJsonObject json;
    json.insert( QLatin1String( "path" ), finding.path );
    json.insert( QLatin1String( "reference_present" ), finding.referencePresent );
    json.insert( QLatin1String( "student_present" ), finding.studentPresent );
    if ( finding.referencePresent )
        json.insert( QLatin1String( "reference_value" ), finding.referenceValue );
    if ( finding.studentPresent )
        json.insert( QLatin1String( "student_value" ), finding.studentValue );
    if ( finding.referencePresent && finding.studentPresent )
        json.insert( QLatin1String( "delta" ), finding.delta );
    return json;
}

/// Deterministic flattening of a metric document into sorted dotted paths of
/// finite numeric leaves (the same leaves metricValueAtPath accepts).
void flattenMetricLeaves( const QJsonObject &object, const QString &prefix,
                          QStringList &out )
{
    for ( auto it = object.begin(); it != object.end(); ++it )
    {
        const QString path = prefix.isEmpty() ? it.key() : prefix + QLatin1Char( '.' ) + it.key();
        if ( it->isObject() )
            flattenMetricLeaves( it->toObject(), path, out );
        else if ( it->isDouble() )
            out << path;
        // arrays and non-numeric leaves are outside the metric-leaf contract
    }
}

} // namespace

QJsonObject ArtifactComparison::toJson() const
{
    return artifactComparisonToJson( *this );
}

QJsonObject MetricDeltaFinding::toJson() const
{
    return metricDeltaToJson( *this );
}

QVector<ArtifactComparison> ArtifactMetricComparer::compareStepOutputs(
    const RunSnapshot &reference, const RunSnapshot &student,
    const AlignmentResult &alignment, int maxEntries )
{
    QHash<QString, const StepSnapshot *> studentById;
    for ( const StepSnapshot &step : student.steps() )
        studentById.insert( step.stepId, &step );

    QVector<ArtifactComparison> comparisons;
    for ( const StepMatch &match : alignment.matches )
    {
        const StepSnapshot *refStep = reference.findStep( match.referenceStepId );
        const StepSnapshot *studentStep = studentById.value( match.studentStepId );
        if ( !refStep || !studentStep )
            continue;
        if ( refStep->outputDigest.isEmpty() && studentStep->outputDigest.isEmpty() )
            continue; // nothing recorded on either side — not a comparison

        ArtifactComparison comparison;
        comparison.referenceStepId = refStep->stepId;
        comparison.studentStepId = studentStep->stepId;
        comparison.operatorId = refStep->operatorId;
        comparison.referenceDigest = refStep->outputDigest;
        comparison.referenceMode = refStep->digestMode;
        comparison.studentDigest = studentStep->outputDigest;
        comparison.studentMode = studentStep->digestMode;
        if ( refStep->outputDigest.isEmpty() || studentStep->outputDigest.isEmpty() )
            comparison.digestVerdict = ArtifactComparison::DigestVerdict::OneSided;
        else if ( refStep->digestMode != studentStep->digestMode )
            comparison.digestVerdict = ArtifactComparison::DigestVerdict::IncomparableModes;
        else
            comparison.digestVerdict = refStep->outputDigest == studentStep->outputDigest
                                           ? ArtifactComparison::DigestVerdict::Equal
                                           : ArtifactComparison::DigestVerdict::Different;

        comparison.sizesComparable = refStep->outputSizeBytes >= 0
                                     && studentStep->outputSizeBytes >= 0;
        comparison.referenceSizeBytes = refStep->outputSizeBytes;
        comparison.studentSizeBytes = studentStep->outputSizeBytes;

        comparisons.append( comparison );
        if ( comparisons.size() >= maxEntries )
            break;
    }
    return comparisons;
}

QVector<MetricDeltaFinding> ArtifactMetricComparer::compareRunMetrics(
    const RunSnapshot &reference, const RunSnapshot &student, int maxLeaves )
{
    QStringList paths;
    flattenMetricLeaves( reference.metrics(), QString(), paths );
    QStringList studentPaths;
    flattenMetricLeaves( student.metrics(), QString(), studentPaths );
    QSet<QString> seen;
    for ( const QString &path : paths )
        seen.insert( path );
    for ( const QString &path : studentPaths )
        if ( !seen.contains( path ) )
            paths << path;
    paths.sort();
    if ( paths.size() > maxLeaves )
        paths.resize( maxLeaves ); // deterministic sorted-order cut

    QVector<MetricDeltaFinding> deltas;
    for ( const QString &path : paths )
    {
        const auto refValue =
            sicnu::experiment::metricValueAtPath( reference.metrics(), path );
        const auto studentValue =
            sicnu::experiment::metricValueAtPath( student.metrics(), path );
        if ( !refValue.has_value() && !studentValue.has_value() )
            continue;
        MetricDeltaFinding finding;
        finding.path = path;
        finding.referencePresent = refValue.has_value();
        finding.studentPresent = studentValue.has_value();
        if ( finding.referencePresent )
            finding.referenceValue = *refValue;
        if ( finding.studentPresent )
            finding.studentValue = *studentValue;
        if ( finding.referencePresent && finding.studentPresent )
            finding.delta = *studentValue - *refValue;
        deltas.append( finding );
    }
    return deltas;
}

} // namespace sicnu::experiment::debugger
