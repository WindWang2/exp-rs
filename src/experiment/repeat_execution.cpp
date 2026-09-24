#include "repeat_execution.h"

#include <QJsonArray>

namespace sicnu::experiment
{

namespace
{

Diagnostic classifierDiag( const QString &message )
{
    return Diagnostic{ QStringLiteral( "experiment.classifier_invalid" ), message,
                       DiagnosticSeverity::Error };
}

/// Environment drift between two snapshots: field name → {original, repeat}.
/// Covers both the allowlisted platform fields and the filtered env
/// variables, namespaced so a field and a variable with the same name cannot
/// merge.
QJsonObject environmentDrift( const RunEnvironment &recorded, const RunEnvironment &repeat )
{
    QJsonObject drift;
    const QJsonObject recordedFields = recorded.fields();
    const QJsonObject repeatFields = repeat.fields();
    for ( auto it = recordedFields.begin(); it != recordedFields.end(); ++it )
    {
        const QJsonValue repeatValue = repeatFields.value( it.key() );
        if ( repeatValue != it.value() )
        {
            QJsonObject entry;
            entry.insert( QStringLiteral( "recorded" ), it.value() );
            entry.insert( QStringLiteral( "repeat" ), repeatValue );
            drift.insert( QStringLiteral( "field:%1" ).arg( it.key() ), entry );
        }
    }
    for ( auto it = repeatFields.begin(); it != repeatFields.end(); ++it )
    {
        if ( !recordedFields.contains( it.key() ) )
        {
            QJsonObject entry;
            entry.insert( QStringLiteral( "recorded" ), QJsonValue() );
            entry.insert( QStringLiteral( "repeat" ), it.value() );
            drift.insert( QStringLiteral( "field:%1" ).arg( it.key() ), entry );
        }
    }
    const QHash<QString, QString> recordedVars = recorded.envVariables();
    const QHash<QString, QString> repeatVars = repeat.envVariables();
    QStringList names;
    names.reserve( recordedVars.size() + repeatVars.size() );
    for ( auto it = recordedVars.begin(); it != recordedVars.end(); ++it )
        names.append( it.key() );
    for ( auto it = repeatVars.begin(); it != repeatVars.end(); ++it )
        if ( !recordedVars.contains( it.key() ) )
            names.append( it.key() );
    names.sort();
    for ( const QString &name : names )
    {
        const QString recordedValue = recordedVars.value( name );
        const QString repeatValue = repeatVars.value( name );
        if ( recordedValue != repeatValue )
        {
            QJsonObject entry;
            entry.insert( QStringLiteral( "recorded" ), recordedValue );
            entry.insert( QStringLiteral( "repeat" ), repeatValue );
            drift.insert( QStringLiteral( "env:%1" ).arg( name ), entry );
        }
    }
    return drift;
}

QString classificationToString( RepeatExecutionClassifier::Classification classification )
{
    using C = RepeatExecutionClassifier::Classification;
    switch ( classification )
    {
        case C::New:
            return QStringLiteral( "new" );
        case C::SameExecution:
            return QStringLiteral( "same_execution" );
        case C::SameIdentity:
            return QStringLiteral( "same_identity" );
        case C::EquivalentRerun:
            return QStringLiteral( "equivalent_rerun" );
        case C::Deviated:
            return QStringLiteral( "deviated" );
    }
    return QStringLiteral( "unknown" );
}

} // namespace

QJsonObject RepeatExecutionClassifier::Verdict::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kRepeatExecutionSchemaVersion );
    json.insert( QStringLiteral( "classification" ),
                 classificationToString( classification ) );
    QJsonArray matched;
    for ( const QString &runId : matchedRunIds )
        matched.append( runId );
    json.insert( QStringLiteral( "matched_run_ids" ), matched );
    if ( !pinComparison.isEmpty() )
        json.insert( QStringLiteral( "pin_comparison" ), pinComparison );
    if ( !environmentDrift.isEmpty() )
        json.insert( QStringLiteral( "environment_drift" ), environmentDrift );
    QJsonArray reasonArray;
    for ( const QString &reason : reasons )
        reasonArray.append( reason );
    json.insert( QStringLiteral( "reasons" ), reasonArray );
    return json;
}

RepeatExecutionClassifier::RepeatExecutionClassifier( ExperimentStore &store )
    : m_store( &store )
{
}

Result<RepeatExecutionClassifier::Verdict> RepeatExecutionClassifier::classify(
    const RunExecutionIdentity &identity, const QString &resultFingerprint,
    const QString &executionRef, const std::optional<RunEnvironment> &repeatEnvironment ) const
{
    using C = Classification;
    if ( !m_store || !m_store->isOpen() )
        return Result<Verdict>::failure(
            classifierDiag( QStringLiteral( "store is not open" ) ) );
    if ( identity.algorithmId.isEmpty() )
        return Result<Verdict>::failure(
            classifierDiag( QStringLiteral( "identity requires an algorithm id" ) ) );

    Verdict verdict;

    // Identity twins via the indexed fingerprint column (12.0): the hash IS
    // the identity semantics, so the index lookup is exact, not heuristic.
    const QString identityHash = runExecutionFingerprint( identity );
    const auto twinLookup = m_store->runIdsByExecutionFingerprint( identityHash, /*limit=*/50 );
    if ( !twinLookup )
        return Result<Verdict>::failure( twinLookup.diagnostics() );
    const QStringList twins = twinLookup.value();

    for ( const QString &runId : twins )
    {
        const std::optional<ExperimentRun> recorded = m_store->runById( runId );
        if ( !recorded )
        {
            // A row whose JSON no longer parses cannot back a verdict.
            verdict.reasons.append(
                QStringLiteral( "matched run %1 is unreadable; excluded from the verdict" )
                    .arg( runId ) );
            continue;
        }
        verdict.matchedRunIds.append( runId );
        // Drift evidence is computed ONCE, against the first matched run.
        if ( repeatEnvironment && verdict.environmentDrift.isEmpty() )
            verdict.environmentDrift =
                environmentDrift( recorded->environment(), *repeatEnvironment );
    }

    // Every identity twin is unreadable: twins.isNotEmpty means the indexed
    // fingerprint matched real rows, so calling this "New" lies about the
    // store, and any duplicate/rerun verdict would be invented from zero
    // readable evidence. Typed failure beats a plausible guess.
    if ( !twins.isEmpty() && verdict.matchedRunIds.isEmpty() )
        return Result< Verdict >::failure( Diagnostic{
            QStringLiteral( "experiment.repeat_unreadable_twin" ),
            QStringLiteral( "%1 identity twin(s) matched the execution fingerprint"
                            " but none of their run records parse; refusing to"
                            " classify on unreadable evidence" )
                .arg( twins.size() ),
            DiagnosticSeverity::Error } );

    if ( twins.isEmpty() )
    {
        // No identity twin. A platform execution that ALREADY recorded runs
        // under other pins is the dangerous case: same ref, different
        // experiment.
        if ( !executionRef.isEmpty() )
        {
            const auto byRefLookup = m_store->runIdsByExecutionRef( executionRef, 10 );
            if ( !byRefLookup )
                return Result<Verdict>::failure( byRefLookup.diagnostics() );
            const QStringList byRef = byRefLookup.value();
            for ( const QString &runId : byRef )
            {
                const std::optional<ExperimentRun> recorded = m_store->runById( runId );
                if ( !recorded )
                    continue;
                ExperimentRun repeatStub;
                repeatStub.setAlgorithmId( identity.algorithmId );
                repeatStub.setAlgorithmVersion( identity.algorithmVersion );
                repeatStub.setParameters( identity.parameters );
                repeatStub.setDatasetVersionId( identity.datasetVersionId );
                repeatStub.setDatasetFingerprint( identity.datasetFingerprint );
                repeatStub.setSplitManifestId( identity.splitManifestId );
                repeatStub.setSplitFingerprint( identity.splitFingerprint );
                repeatStub.setModelDigest( identity.modelDigest );
                repeatStub.setSeed( identity.seed );
                repeatStub.setSoftwareRevision( identity.softwareRevision );
                verdict.classification = C::Deviated;
                verdict.matchedRunIds.append( runId );
                verdict.pinComparison =
                    RunComparison::compare( *recorded, repeatStub ).toJson();
                verdict.reasons.append(
                    QStringLiteral( "execution ref %1 already recorded run %2"
                                    " with different identity pins" )
                        .arg( executionRef, runId ) );
                break;
            }
        }
        if ( verdict.classification != C::Deviated )
        {
            verdict.classification = C::New;
            verdict.reasons.append( QStringLiteral( "no recorded run shares the identity" ) );
        }
        return Result<Verdict>::success( verdict );
    }

    // Identity twins exist. Result evidence decides duplicate vs rerun.
    if ( resultFingerprint.isEmpty() )
    {
        verdict.classification = C::SameIdentity;
        verdict.reasons.append(
            QStringLiteral( "identity matches %1 recorded run(s); supply the result"
                            " fingerprint to decide duplicate vs rerun" )
                .arg( verdict.matchedRunIds.size() ) );
        return Result<Verdict>::success( verdict );
    }

    QStringList sameResult;
    for ( const QString &runId : verdict.matchedRunIds )
    {
        const std::optional<ExperimentRun> recorded = m_store->runById( runId );
        if ( recorded && recorded->resultFingerprint() == resultFingerprint )
            sameResult.append( runId );
    }
    if ( !sameResult.isEmpty() )
    {
        verdict.classification = C::SameExecution;
        verdict.matchedRunIds = sameResult;
        verdict.reasons.append(
            QStringLiteral( "identity and result fingerprint match recorded run(s)" ) );
        return Result<Verdict>::success( verdict );
    }

    verdict.classification = C::EquivalentRerun;
    const std::optional<ExperimentRun> first = m_store->runById( verdict.matchedRunIds.first() );
    if ( first )
    {
        if ( first->determinism() == DeterminismGrade::Strict )
        {
            verdict.reasons.append(
                QStringLiteral( "identity matches run %1 declared Strict-deterministic,"
                                " yet results differ — treat as evidence of drift, not a"
                                " benign rerun" )
                    .arg( first->runId() ) );
        }
        else
        {
            verdict.reasons.append(
                QStringLiteral( "identity matches run %1 with declared nondeterminism"
                                " (note: %2)" )
                    .arg( first->runId(),
                          first->determinismNote().isEmpty()
                              ? QStringLiteral( "<none>" )
                              : first->determinismNote() ) );
        }
    }
    return Result<Verdict>::success( verdict );
}

} // namespace sicnu::experiment
