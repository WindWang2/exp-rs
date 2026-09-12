// replay_deviation.cpp — Replay deviation reporting (goal M7).
//
// Pure read-side projection over two recorded runs. The Incomplete verdict
// exists because "cannot tell" is a scientific answer; inventing a verdict
// from partial evidence is not.
#include "replay_deviation.h"

#include <QJsonArray>

namespace sicnu::experiment
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

namespace
{

Diagnostic deviationError( const QString &message )
{
    return Diagnostic{ QStringLiteral( "experiment.replay_unknown_run" ), message,
                       DiagnosticSeverity::Error };
}

void collectEnvironmentDrift( const ExperimentRun &original, const ExperimentRun &replay,
                              QJsonObject &drift, QStringList &deviations )
{
    const QJsonObject originalFields = original.environment().redacted().toJson();
    const QJsonObject replayFields = replay.environment().redacted().toJson();
    QStringList names;
    for ( auto it = originalFields.constBegin(); it != originalFields.constEnd(); ++it )
        names.append( it.key() );
    for ( auto it = replayFields.constBegin(); it != replayFields.constEnd(); ++it )
    {
        if ( !names.contains( it.key() ) )
            names.append( it.key() );
    }
    names.sort();
    for ( const QString &name : names )
    {
        const QJsonValue a = originalFields.value( name );
        const QJsonValue b = replayFields.value( name );
        if ( a == b )
            continue;
        QJsonObject entry;
        entry.insert( QStringLiteral( "original" ), a );
        entry.insert( QStringLiteral( "replay" ), b );
        drift.insert( name, entry );
        deviations.append( QStringLiteral( "environment field '%1' changed" ).arg( name ) );
    }
}

} // namespace

QJsonObject ReplayDeviationReport::toJson() const
{
    const auto verdictString = []( Verdict verdict ) {
        switch ( verdict )
        {
            case Verdict::Identical:
                return QStringLiteral( "identical" );
            case Verdict::Equivalent:
                return QStringLiteral( "equivalent" );
            case Verdict::Deviated:
                return QStringLiteral( "deviated" );
            case Verdict::Incomplete:
                return QStringLiteral( "incomplete" );
        }
        return QStringLiteral( "incomplete" );
    };

    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kReplayDeviationSchemaVersion );
    json.insert( QStringLiteral( "verdict" ), verdictString( verdict ) );
    json.insert( QStringLiteral( "original_run_id" ), originalRunId );
    json.insert( QStringLiteral( "replay_run_id" ), replayRunId );
    json.insert( QStringLiteral( "pin_comparison" ), pinComparison );
    json.insert( QStringLiteral( "metric_delta" ), metricDelta );
    json.insert( QStringLiteral( "environment_drift" ), environmentDrift );
    json.insert( QStringLiteral( "deviations" ),
                 QJsonArray::fromStringList( deviations ) );
    json.insert( QStringLiteral( "evidence_gaps" ),
                 QJsonArray::fromStringList( evidenceGaps ) );
    return json;
}

ReplayDeviationAnalyzer::ReplayDeviationAnalyzer( ExperimentStore &store )
    : m_store( &store )
{
}

Result<ReplayDeviationReport> ReplayDeviationAnalyzer::analyze( const QString &originalRunId,
                                                                const QString &replayRunId ) const
{
    const auto original = m_store->runById( originalRunId );
    if ( !original )
        return Result<ReplayDeviationReport>::failure(
            deviationError( QStringLiteral( "original run %1 is not in the store" )
                                .arg( originalRunId ) ) );
    const auto replay = m_store->runById( replayRunId );
    if ( !replay )
        return Result<ReplayDeviationReport>::failure(
            deviationError( QStringLiteral( "replay run %1 is not in the store" )
                                .arg( replayRunId ) ) );

    ReplayDeviationReport report;
    report.originalRunId = originalRunId;
    report.replayRunId = replayRunId;

    const RunComparison comparison = RunComparison::compare( original.value(), replay.value() );
    report.pinComparison = comparison.toJson();

    // Only IDENTITY pins drive the verdict. The M6 artifacts/runtime
    // dimensions ride the comparison as diagnostics (review round 1): wall
    // time beyond the jitter policy or an output-digest drift on equal pins
    // is reported, but must not flip a same-identity replay to "deviated" —
    // that verdict is reserved for identity, per the header contract.
    static const QStringList kIdentityDimensions{
        QStringLiteral( "dataset" ), QStringLiteral( "split" ),
        QStringLiteral( "model" ),   QStringLiteral( "algorithm" ),
        QStringLiteral( "config" ),  QStringLiteral( "seed" ),
        QStringLiteral( "environment" ),
    };
    for ( const RunDiffItem &item : comparison.dimensions )
    {
        if ( item.differs && kIdentityDimensions.contains( item.dimension ) )
            report.deviations.append(
                QStringLiteral( "pin '%1' differs: %2" ).arg( item.dimension, item.detail ) );
    }
    // Diagnostics (artifacts/runtime) still surface — as deviations of
    // record when identity is equal, without deciding the verdict alone.
    for ( const RunDiffItem &item : comparison.dimensions )
    {
        if ( item.differs && !kIdentityDimensions.contains( item.dimension ) )
            report.deviations.append(
                QStringLiteral( "diagnostic '%1' differs: %2" )
                    .arg( item.dimension, item.detail ) );
    }

    // Evidence completeness gates the verdict strength.
    if ( original->status() != RunStatus::Completed ||
         replay->status() != RunStatus::Completed )
        report.evidenceGaps.append( QStringLiteral( "both runs must be Completed" ) );
    if ( original->artifacts().isEmpty() || replay->artifacts().isEmpty() )
        report.evidenceGaps.append( QStringLiteral( "artifact evidence missing on one side" ) );

    collectEnvironmentDrift( original.value(), replay.value(), report.environmentDrift,
                             report.deviations );

    if ( !report.evidenceGaps.isEmpty() )
    {
        report.verdict = ReplayDeviationReport::Verdict::Incomplete;
    }
    else if ( !report.deviations.isEmpty() )
    {
        report.verdict = ReplayDeviationReport::Verdict::Deviated;
    }
    else if ( original->resultFingerprint() == replay->resultFingerprint() )
    {
        report.verdict = ReplayDeviationReport::Verdict::Identical;
        report.metricDelta = comparison.metricDiff( original.value(), replay.value() );
    }
    else if ( original->determinism() != DeterminismGrade::Strict ||
              replay->determinism() != DeterminismGrade::Strict )
    {
        // Same identity, different outputs: legitimate only under a declared
        // determinism grade below Strict (either side claiming Strict makes
        // the mismatch a deviation).
        report.verdict = ReplayDeviationReport::Verdict::Equivalent;
        report.metricDelta = comparison.metricDiff( original.value(), replay.value() );
    }
    else
    {
        report.verdict = ReplayDeviationReport::Verdict::Deviated;
        report.deviations.append(
            QStringLiteral( "result fingerprints differ under Strict determinism" ) );
    }
    return Result<ReplayDeviationReport>::success( report );
}

} // namespace sicnu::experiment
