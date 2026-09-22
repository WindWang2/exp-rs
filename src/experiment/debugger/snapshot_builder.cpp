// snapshot_builder.cpp — RunSnapshot assembly (RS14-06, ADR 0174).
//
// Slice A GREEN: joins the run-level record with the richest available step
// evidence, computes canonical parameter hashes, enforces caps, and projects
// identity pins. Absent step evidence is a valid outcome; evidence errors
// (malformed / too large) propagate typed.

#include "snapshot_builder.h"

#include "../../dataset/dataset_types.h"
#include "../../data/execution_fingerprint.h"


namespace sicnu::experiment::debugger
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

namespace
{

Diagnostic typedFailure( const char *code, const QString &message )
{
    return { QLatin1String( code ), message, DiagnosticSeverity::Error };
}

bool carriesCode( const QVector<Diagnostic> &diagnostics, const char *code )
{
    for ( const Diagnostic &diagnostic : diagnostics )
        if ( diagnostic.code == QLatin1String( code ) )
            return true;
    return false;
}

} // namespace

RunSnapshotBuilder::RunSnapshotBuilder( IRunEvidenceSource &source )
    : m_source( &source )
{
}

Result<RunSnapshot> RunSnapshotBuilder::build( const QString &runId ) const
{
    auto record = m_source->run( runId );
    if ( !record )
        return Result<RunSnapshot>::failure( record.diagnostics() );

    RunSnapshot snapshot;
    snapshot.setRunId( runId );

    QVector<Diagnostic> warnings;
    auto evidence = m_source->steps( runId );
    if ( evidence.has_value() )
    {
        // Normalizer warnings (e.g. recorder-side step truncation) ride into
        // the report — degrade-in-the-open end to end.
        for ( const Diagnostic &diagnostic : evidence.diagnostics() )
            if ( diagnostic.code != QLatin1String( kCodeEvidenceAbsent ) )
                warnings.append( diagnostic );
        StepEvidence steps = evidence.take();
        if ( steps.steps.size() > kMaxSnapshotSteps )
            return Result<RunSnapshot>::failure( typedFailure(
                kCodeEvidenceTooLarge,
                QStringLiteral( "%1 steps exceed the snapshot cap of %2" )
                    .arg( steps.steps.size() ).arg( kMaxSnapshotSteps ) ) );
        if ( steps.artifacts.size() > kMaxSnapshotArtifacts )
            return Result<RunSnapshot>::failure( typedFailure(
                kCodeEvidenceTooLarge,
                QStringLiteral( "%1 artifacts exceed the snapshot cap of %2" )
                    .arg( steps.artifacts.size() ).arg( kMaxSnapshotArtifacts ) ) );

        QVector<StepSnapshot> normalized;
        normalized.reserve( steps.steps.size() );
        for ( StepSnapshot &step : steps.steps )
        {
            // Canonical parameter identity — key order must not matter.
            // Single parameter-identity truth: the platform's canonical
            // config hash (experiment_types.h), not a local re-implementation.
            if ( step.paramsHash.isEmpty() && !step.parameters.isEmpty() )
                step.paramsHash = sicnu::experiment::runConfigHash( step.parameters );
            normalized.append( step );
        }
        snapshot.setSteps( normalized );
        snapshot.setArtifacts( steps.artifacts );
        snapshot.setPlanSignature( steps.planSignature );
        snapshot.setStepEvidence( steps.mode );
    }
    else
    {
        // Absence is a valid, honestly-degraded outcome; any OTHER evidence
        // failure is a hard typed error and stops the build here.
        if ( !carriesCode( evidence.diagnostics(), kCodeEvidenceAbsent ) )
            return Result<RunSnapshot>::failure( evidence.diagnostics() );
        for ( const Diagnostic &diagnostic : evidence.diagnostics() )
            if ( diagnostic.code != QLatin1String( kCodeEvidenceAbsent ) )
                warnings.append( diagnostic );
        snapshot.setStepEvidence( StepEvidenceMode::Absent );
    }

    // Identity pins projected verbatim from the run record.
    RunPinsSnapshot pins;
    pins.algorithmId = record->algorithmId();
    pins.algorithmVersion = record->algorithmVersion();
    pins.datasetVersionId = record->datasetVersionId();
    pins.datasetFingerprint = record->datasetFingerprint();
    pins.splitManifestId = record->splitManifestId();
    pins.splitFingerprint = record->splitFingerprint();
    pins.modelId = record->modelId();
    pins.modelDigest = record->modelDigest();
    pins.softwareRevision = record->softwareRevision();
    pins.configHash = record->configHash();
    pins.resultFingerprint = record->resultFingerprint();
    pins.runStatus = sicnu::dataset::runStatusToString( record->status() );
    // Representation honesty: ExperimentRun carries a bare quint64 seed, so a
    // legitimately-recorded seed 0 is indistinguishable from "no seed"; it is
    // reported as unknown rather than guessed.
    pins.seed = record->seed();
    pins.seedKnown = record->seed() != 0;
    snapshot.setPins( pins );
    snapshot.setMetrics( record->metrics() );

    return Result<RunSnapshot>::success( snapshot, warnings );
}

} // namespace sicnu::experiment::debugger
