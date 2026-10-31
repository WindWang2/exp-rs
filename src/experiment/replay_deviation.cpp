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

// --- v2: domain attribution + remediation for every deviation ---------------

ReplayDeviationDomain domainOf( const QString &dimension )
{
    if ( dimension == QLatin1String( "dataset" ) || dimension == QLatin1String( "split" ) )
        return ReplayDeviationDomain::Data;
    if ( dimension == QLatin1String( "model" ) )
        return ReplayDeviationDomain::Model;
    if ( dimension == QLatin1String( "environment" ) )
        return ReplayDeviationDomain::Environment;
    // config / seed / algorithm and the artifacts / runtime diagnostics all
    // describe the experiment's own execution record.
    return ReplayDeviationDomain::Experiment;
}

QString remediationFor( const QString &dimension, bool identityPin )
{
    if ( dimension == QLatin1String( "dataset" ) )
        return QStringLiteral(
            "pin the original dataset version id before replaying (the replay must "
            "read the same dataset version, not a re-imported sibling)" );
    if ( dimension == QLatin1String( "split" ) )
        return QStringLiteral(
            "regenerate the split from the recorded split manifest/seed instead of "
            "re-partitioning, so train/holdout membership is identical" );
    if ( dimension == QLatin1String( "model" ) )
        return QStringLiteral(
            "resolve the same model identity@version and content digest before "
            "replaying; a re-trained or re-serialized model is a different model" );
    if ( dimension == QLatin1String( "algorithm" ) )
        return QStringLiteral(
            "run the same algorithm/operator version as the original (the revision "
            "pin is part of the experiment identity)" );
    if ( dimension == QLatin1String( "config" ) )
        return QStringLiteral(
            "diff the canonical parameter JSON of both runs and align the replay "
            "with the recorded configuration" );
    if ( dimension == QLatin1String( "seed" ) )
        return QStringLiteral( "re-run with the recorded random seed" );
    if ( dimension == QLatin1String( "environment" ) )
        return QStringLiteral(
            "environment drift is reported, not punished; align the drifted fields "
            "only when the experiment declares Strict determinism" );
    if ( dimension == QLatin1String( "artifacts" ) )
        return QStringLiteral(
            "output digests differ on otherwise-equal identity: compare the two "
            "artifact sets directly (this is a record, not an identity break)" );
    if ( dimension == QLatin1String( "runtime" ) )
        return QStringLiteral(
            "wall time beyond the jitter policy on otherwise-equal identity: "
            "informational only — performance reruns are expected to drift" );
    if ( dimension == QLatin1String( "result" ) )
        return QStringLiteral(
            "all identity pins are equal but the result fingerprints differ under "
            "Strict determinism: audit threading/vendor-kernel nondeterminism or "
            "declare the true determinism grade in the run record" );
    return identityPin
             ? QStringLiteral( "align this identity pin with the recorded original "
                               "before treating the replay as the same experiment" )
             : QStringLiteral( "review the recorded diagnostic before consuming the replay" );
}

/// Appends one deviation in BOTH forms: the exact v1 human string (kept
/// byte-identical for existing consumers) and the typed v2 entry.
void appendDeviation( ReplayDeviationReport &report, const QString &dimension,
                      bool identityPin, const QString &v1String, const QString &detail )
{
    report.deviations.append( v1String );
    ReplayDeviationEntry entry;
    entry.dimension = dimension;
    entry.domain = domainOf( dimension );
    entry.identityPin = identityPin;
    entry.detail = detail;
    entry.remediation = remediationFor( dimension, identityPin );
    report.deviationDetails.append( entry );
}

void collectEnvironmentDrift( const ExperimentRun &original, const ExperimentRun &replay,
                              QJsonObject &drift )
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
    }
}

} // namespace

namespace
{

QString domainToString( ReplayDeviationDomain domain )
{
    switch ( domain )
    {
        case ReplayDeviationDomain::Data:
            return QStringLiteral( "data" );
        case ReplayDeviationDomain::Model:
            return QStringLiteral( "model" );
        case ReplayDeviationDomain::Experiment:
            return QStringLiteral( "experiment" );
        case ReplayDeviationDomain::Environment:
            return QStringLiteral( "environment" );
    }
    return QStringLiteral( "experiment" );
}

} // namespace

QJsonObject ReplayDeviationEntry::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "dimension" ), dimension );
    json.insert( QStringLiteral( "domain" ), domainToString( domain ) );
    json.insert( QStringLiteral( "identity_pin" ), identityPin );
    json.insert( QStringLiteral( "detail" ), detail );
    json.insert( QStringLiteral( "remediation" ), remediation );
    return json;
}

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
    QJsonArray details;
    for ( const ReplayDeviationEntry &entry : deviationDetails )
        details.append( entry.toJson() );
    json.insert( QStringLiteral( "deviation_details" ), details );
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
    bool identityDiffers = false;
    for ( const RunDiffItem &item : comparison.dimensions )
    {
        if ( item.differs && kIdentityDimensions.contains( item.dimension ) )
        {
            identityDiffers = true;
            appendDeviation(
                report, item.dimension, true,
                QStringLiteral( "pin '%1' differs: %2" ).arg( item.dimension, item.detail ),
                item.detail );
        }
    }
    // Diagnostics (artifacts/runtime) still surface — as deviations of
    // record when identity is equal, without deciding the verdict alone.
    for ( const RunDiffItem &item : comparison.dimensions )
    {
        if ( item.differs && !kIdentityDimensions.contains( item.dimension ) )
            appendDeviation(
                report, item.dimension, false,
                QStringLiteral( "diagnostic '%1' differs: %2" ).arg( item.dimension, item.detail ),
                item.detail );
    }

    // Evidence completeness gates the verdict strength.
    if ( original->status() != RunStatus::Completed ||
         replay->status() != RunStatus::Completed )
        report.evidenceGaps.append( QStringLiteral( "both runs must be Completed" ) );
    if ( original->artifacts().isEmpty() || replay->artifacts().isEmpty() )
        report.evidenceGaps.append( QStringLiteral( "artifact evidence missing on one side" ) );

    collectEnvironmentDrift( original.value(), replay.value(), report.environmentDrift );
    for ( const QString &field : report.environmentDrift.keys() )
    {
        appendDeviation(
            report, QStringLiteral( "environment" ), true,
            QStringLiteral( "environment field '%1' changed" ).arg( field ),
            QStringLiteral( "field '%1' changed between the runs" ).arg( field ) );
    }

    if ( !report.evidenceGaps.isEmpty() )
    {
        report.verdict = ReplayDeviationReport::Verdict::Incomplete;
    }
    else if ( identityDiffers )
    {
        // Identity-only gate, per the header contract: "deviated" means at
        // least one identity pin differs. The artifacts/runtime diagnostics
        // above ride along as deviations of record — wall time beyond the
        // jitter policy or a digest-set drift on equal pins is reported, but
        // never flips a same-identity replay by itself.
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
        appendDeviation(
            report, QStringLiteral( "result" ), true,
            QStringLiteral( "result fingerprints differ under Strict determinism" ),
            QStringLiteral( "result fingerprints differ under Strict determinism" ) );
    }
    return Result<ReplayDeviationReport>::success( report );
}

} // namespace sicnu::experiment
