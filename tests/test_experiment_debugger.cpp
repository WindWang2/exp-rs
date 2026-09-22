// test_experiment_debugger.cpp — RS14-06 Experiment Debugger contract tests.
//
// Slice A: normalized run snapshot + canonical digest.
// Each TEST_CASE pins one contract of the public seam
// (RunSnapshotBuilder / RunSnapshot / evidence sources). Adversarial-oracle
// policy: assertions name exact step ids, modes and digests — a vacuous
// "some evidence somewhere" pass cannot satisfy these suites.

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "data/data_result.h"
#include "experiment/debugger/debugger_types.h"
#include "experiment/debugger/evidence_source.h"
#include "experiment/debugger/run_snapshot.h"
#include "experiment/debugger/snapshot_builder.h"

#include "experiment_debugger_fixtures.h"

using namespace sicnu::experiment::debugger;
using namespace sicnu::experiment::debugger::fixtures;
using sicnu::experiment::ExperimentRun;

namespace
{

/// Standard three-step lab pipeline as a provenance document:
///   input scene -> (ndvi) -> (threshold) -> (area)
/// with a raw-scene root input consumed by the first step.
struct ProvPipeline
{
    QJsonObject doc;
    QString planSignature;
};

ProvPipeline makePipelineDoc( const QString &runId,
                              const QString &ndviLineage = QStringLiteral( "sig-ndvi" ),
                              const QString &thresholdLineage = QStringLiteral( "sig-threshold" ),
                              const QString &areaLineage = QStringLiteral( "sig-area" ) )
{
    ProvPipeline pipeline;
    pipeline.planSignature = QStringLiteral( "plan-%1" ).arg( runId );
    pipeline.doc = makeProvenanceDoc(
        runId, pipeline.planSignature,
        {
            { QStringLiteral( "ndvi" ), QStringLiteral( "rs:ndvi" ), QStringLiteral( "Succeeded" ),
              ndviLineage, false, QStringLiteral( "/lab/%1/ndvi.tif" ).arg( runId ),
              QStringLiteral( "sha256fl:aa11" ), 2048 },
            { QStringLiteral( "threshold" ), QStringLiteral( "rs:threshold_calc" ), QStringLiteral( "Succeeded" ),
              thresholdLineage, false, QStringLiteral( "/lab/%1/binary.tif" ).arg( runId ),
              QStringLiteral( "sha256fl:bb22" ), 1024 },
            { QStringLiteral( "area" ), QStringLiteral( "rs:area_stats" ), QStringLiteral( "Succeeded" ),
              areaLineage, false, QStringLiteral( "/lab/%1/area.json" ).arg( runId ),
              QStringLiteral( "sha256fl:cc33" ), 128 },
        },
        {
            // root input: raw scene (no in-run producer)
            { QStringLiteral( "ndvi" ), QStringLiteral( "/data/raw/scene.tif" ),
              QStringLiteral( "sha256fl:dead" ), 999999 },
            { QStringLiteral( "threshold" ), QStringLiteral( "/lab/%1/ndvi.tif" ).arg( runId ),
              QStringLiteral( "sha256fl:aa11" ), 2048 },
            { QStringLiteral( "area" ), QStringLiteral( "/lab/%1/binary.tif" ).arg( runId ),
              QStringLiteral( "sha256fl:bb22" ), 1024 },
        } );
    return pipeline;
}

ExperimentRun makeStoredRun( const QString &runId )
{
    ExperimentRun run = makeRunRecord( runId );
    run.setStatus( sicnu::experiment::RunStatus::Completed );
    return run;
}

} // namespace

TEST_CASE( "Slice A: snapshot builds from a provenance graph document", "[debugger][sliceA]" )
{
    InMemoryEvidenceSource source;
    source.insertRun( makeStoredRun( QStringLiteral( "run-1" ) ) );
    const ProvPipeline pipeline = makePipelineDoc( QStringLiteral( "run-1" ) );
    source.insertProvenanceDoc( QStringLiteral( "run-1" ), pipeline.doc );

    RunSnapshotBuilder builder( source );
    auto snapshot = builder.build( QStringLiteral( "run-1" ) );

    REQUIRE( snapshot.has_value() );
    REQUIRE( snapshot->stepEvidence() == StepEvidenceMode::ProvenanceGraph );
    REQUIRE( snapshot->planSignature() == pipeline.planSignature );

    // Exact normalization: three steps, topological order, no extras.
    REQUIRE( snapshot->steps().size() == 3 );
    REQUIRE( snapshot->steps().at( 0 ).stepId == QStringLiteral( "ndvi" ) );
    REQUIRE( snapshot->steps().at( 1 ).stepId == QStringLiteral( "threshold" ) );
    REQUIRE( snapshot->steps().at( 2 ).stepId == QStringLiteral( "area" ) );

    const StepSnapshot &ndvi = snapshot->steps().at( 0 );
    REQUIRE( ndvi.operatorId == QStringLiteral( "rs:ndvi" ) );
    REQUIRE( ndvi.lineageSignature == QStringLiteral( "sig-ndvi" ) );
    REQUIRE( ndvi.status == QStringLiteral( "Succeeded" ) );
    // Root input has no in-run producer: ndvi carries no dependencies.
    REQUIRE( ndvi.dependencies.isEmpty() );
    REQUIRE( ndvi.outputDigest == QStringLiteral( "sha256fl:aa11" ) );
    REQUIRE( ndvi.digestMode == QStringLiteral( "sha256fl" ) );

    // Downstream dependencies point at their producers.
    REQUIRE( snapshot->steps().at( 1 ).dependencies ==
             QStringList{ QStringLiteral( "ndvi" ) } );
    REQUIRE( snapshot->steps().at( 2 ).dependencies ==
             QStringList{ QStringLiteral( "threshold" ) } );

    // Root input artifact is recorded as external input state.
    bool foundRoot = false;
    for ( const ArtifactSnapshot &artifact : snapshot->artifacts() )
    {
        if ( artifact.rootInput )
        {
            foundRoot = true;
            REQUIRE( artifact.digest == QStringLiteral( "sha256fl:dead" ) );
            REQUIRE( artifact.digestMode == QStringLiteral( "sha256fl" ) );
        }
    }
    REQUIRE( foundRoot );
}

TEST_CASE( "Slice A: provenance steps carry no fabricated parameters", "[debugger][sliceA]" )
{
    InMemoryEvidenceSource source;
    source.insertRun( makeStoredRun( QStringLiteral( "run-1" ) ) );
    source.insertProvenanceDoc( QStringLiteral( "run-1" ), makePipelineDoc( QStringLiteral( "run-1" ) ).doc );

    RunSnapshotBuilder builder( source );
    auto snapshot = builder.build( QStringLiteral( "run-1" ) );
    REQUIRE( snapshot.has_value() );
    // Provenance graphs do not record parameters; the snapshot must say so
    // honestly (empty parameters AND empty paramsHash), never invent hashes.
    for ( const StepSnapshot &step : snapshot->steps() )
    {
        REQUIRE( step.parameters.isEmpty() );
        REQUIRE( step.paramsHash.isEmpty() );
    }
}

TEST_CASE( "Slice A: canonical digest identity semantics", "[debugger][sliceA]" )
{
    InMemoryEvidenceSource sourceA;
    InMemoryEvidenceSource sourceB;
    sourceA.insertRun( makeStoredRun( QStringLiteral( "run-a" ) ) );
    sourceB.insertRun( makeStoredRun( QStringLiteral( "run-b" ) ) );

    // Same pipeline shape, different run ids: step ids are labels of the same
    // process; the DIGEST excludes volatile fields but INCLUDES structure.
    sourceA.insertProvenanceDoc( QStringLiteral( "run-a" ), makePipelineDoc( QStringLiteral( "run-a" ) ).doc );
    sourceB.insertProvenanceDoc( QStringLiteral( "run-b" ), makePipelineDoc( QStringLiteral( "run-b" ) ).doc );

    RunSnapshotBuilder builderA( sourceA );
    RunSnapshotBuilder builderB( sourceB );
    auto snapshotA = builderA.build( QStringLiteral( "run-a" ) );
    auto snapshotB = builderB.build( QStringLiteral( "run-b" ) );
    REQUIRE( snapshotA.has_value() );
    REQUIRE( snapshotB.has_value() );
    // Different run ids -> different pin seeds -> different digests (documented:
    // run identity participates; digest is per-run, not per-process-shape).
    REQUIRE( snapshotA->snapshotDigest() != snapshotB->snapshotDigest() );

    // Rebuild of the SAME evidence is byte-stable.
    auto snapshotA2 = builderA.build( QStringLiteral( "run-a" ) );
    REQUIRE( snapshotA2.has_value() );
    REQUIRE( snapshotA2->snapshotDigest() == snapshotA->snapshotDigest() );
}

TEST_CASE( "Slice A: digest separates volatile fields from identity", "[debugger][sliceA]" )
{
    // Two snapshots differing ONLY in timing-ish fields (output size, error
    // text) must share a digest; differing in output DIGEST must not.
    auto evidence = []( const char *outputDigest, qint64 size, const char *error ) {
        StepEvidence stepEvidence;
        stepEvidence.mode = StepEvidenceMode::CheckpointSteps;
        StepSnapshot step;
        step.stepId = QStringLiteral( "threshold" );
        step.operatorId = QStringLiteral( "rs:threshold_calc" );
        step.lineageSignature = QStringLiteral( "sig-1" );
        step.status = QStringLiteral( "Completed" );
        step.outputDigest = QString::fromLatin1( outputDigest );
        step.digestMode = QLatin1String( kDigestModeSha256Hex );
        step.outputSizeBytes = size;
        step.errorMessage = QString::fromLatin1( error );
        stepEvidence.steps.append( step );
        return stepEvidence;
    };

    InMemoryEvidenceSource base;
    base.insertRun( makeStoredRun( QStringLiteral( "run-1" ) ) );
    base.insertStepEvidence( QStringLiteral( "run-1" ),
                             evidence( "d1", 1024, "" ) );
    RunSnapshotBuilder builderBase( base );
    auto s1 = builderBase.build( QStringLiteral( "run-1" ) );
    REQUIRE( s1.has_value() );

    // volatile-only twin
    InMemoryEvidenceSource twin;
    twin.insertRun( makeStoredRun( QStringLiteral( "run-1" ) ) );
    twin.insertStepEvidence( QStringLiteral( "run-1" ),
                             evidence( "d1", 4096, "transient warning" ) );
    RunSnapshotBuilder builderTwin( twin );
    auto s2 = builderTwin.build( QStringLiteral( "run-1" ) );
    REQUIRE( s2.has_value() );
    REQUIRE( s2->snapshotDigest() == s1->snapshotDigest() );

    // identity twin (different produced digest)
    InMemoryEvidenceSource changed;
    changed.insertRun( makeStoredRun( QStringLiteral( "run-1" ) ) );
    changed.insertStepEvidence( QStringLiteral( "run-1" ),
                                evidence( "d2", 1024, "" ) );
    RunSnapshotBuilder builderChanged( changed );
    auto s3 = builderChanged.build( QStringLiteral( "run-1" ) );
    REQUIRE( s3.has_value() );
    REQUIRE( s3->snapshotDigest() != s1->snapshotDigest() );
}

TEST_CASE( "Slice A: unknown run fails typed, not thrown", "[debugger][sliceA]" )
{
    InMemoryEvidenceSource source;
    RunSnapshotBuilder builder( source );
    auto snapshot = builder.build( QStringLiteral( "nope" ) );
    REQUIRE( !snapshot.has_value() );
    REQUIRE( snapshot.diagnostics().size() == 1 );
    REQUIRE( snapshot.diagnostics().front().code == QLatin1String( kCodeUnknownRun ) );
}

TEST_CASE( "Slice A: run without step evidence is an honest Absent snapshot",
           "[debugger][sliceA]" )
{
    InMemoryEvidenceSource source;
    source.insertRun( makeStoredRun( QStringLiteral( "run-1" ) ) );

    RunSnapshotBuilder builder( source );
    auto snapshot = builder.build( QStringLiteral( "run-1" ) );
    REQUIRE( snapshot.has_value() );
    REQUIRE( snapshot->stepEvidence() == StepEvidenceMode::Absent );
    REQUIRE( snapshot->steps().isEmpty() );
    // Run-level pins ARE projected even without step evidence.
    REQUIRE( snapshot->pins().algorithmId == QStringLiteral( "workflow:ndvi-threshold-area" ) );
    REQUIRE( snapshot->pins().datasetVersionId == QStringLiteral( "dsv-1" ) );
    REQUIRE( snapshot->pins().seedKnown );
    REQUIRE( snapshot->pins().seed == 7 );
}

TEST_CASE( "Slice A: checkpoint-mode evidence normalizes with parameter hashes",
           "[debugger][sliceA]" )
{
    auto evidenceWithParams = []( QJsonObject params ) {
        StepEvidence stepEvidence;
        stepEvidence.mode = StepEvidenceMode::CheckpointSteps;
        StepSnapshot step;
        step.stepId = QStringLiteral( "threshold" );
        step.operatorId = QStringLiteral( "rs:threshold_calc" );
        step.parameters = params;
        step.lineageSignature = QStringLiteral( "sig-1" );
        step.status = QStringLiteral( "Completed" );
        step.outputDigest = QStringLiteral( "ddd" );
        step.digestMode = QLatin1String( kDigestModeSha256Hex );
        stepEvidence.steps.append( step );
        return stepEvidence;
    };

    // Key order must not matter for the canonical params hash.
    QJsonObject paramsA;
    paramsA.insert( QStringLiteral( "threshold" ), 0.35 );
    paramsA.insert( QStringLiteral( "band" ), QStringLiteral( "nir" ) );
    QJsonObject paramsB;
    paramsB.insert( QStringLiteral( "band" ), QStringLiteral( "nir" ) );
    paramsB.insert( QStringLiteral( "threshold" ), 0.35 );

    InMemoryEvidenceSource sourceA;
    sourceA.insertRun( makeStoredRun( QStringLiteral( "run-a" ) ) );
    sourceA.insertStepEvidence( QStringLiteral( "run-a" ), evidenceWithParams( paramsA ) );
    InMemoryEvidenceSource sourceB;
    sourceB.insertRun( makeStoredRun( QStringLiteral( "run-b" ) ) );
    sourceB.insertStepEvidence( QStringLiteral( "run-b" ), evidenceWithParams( paramsB ) );

    RunSnapshotBuilder builderA( sourceA );
    RunSnapshotBuilder builderB( sourceB );
    auto snapshotA = builderA.build( QStringLiteral( "run-a" ) );
    auto snapshotB = builderB.build( QStringLiteral( "run-b" ) );
    REQUIRE( snapshotA.has_value() );
    REQUIRE( snapshotB.has_value() );
    REQUIRE( snapshotA->steps().size() == 1 );
    REQUIRE( snapshotB->steps().size() == 1 );
    REQUIRE( !snapshotA->steps().at( 0 ).paramsHash.isEmpty() );
    REQUIRE( snapshotA->steps().at( 0 ).paramsHash == snapshotB->steps().at( 0 ).paramsHash );

    // A different parameter value changes the hash.
    QJsonObject paramsC = paramsA;
    paramsC.insert( QStringLiteral( "threshold" ), 0.62 );
    InMemoryEvidenceSource sourceC;
    sourceC.insertRun( makeStoredRun( QStringLiteral( "run-c" ) ) );
    sourceC.insertStepEvidence( QStringLiteral( "run-c" ), evidenceWithParams( paramsC ) );
    RunSnapshotBuilder builderC( sourceC );
    auto snapshotC = builderC.build( QStringLiteral( "run-c" ) );
    REQUIRE( snapshotC.has_value() );
    REQUIRE( snapshotC->steps().at( 0 ).paramsHash != snapshotA->steps().at( 0 ).paramsHash );
}

TEST_CASE( "Slice A: snapshot JSON round-trips and refuses foreign envelopes",
           "[debugger][sliceA]" )
{
    InMemoryEvidenceSource source;
    source.insertRun( makeStoredRun( QStringLiteral( "run-1" ) ) );
    source.insertProvenanceDoc( QStringLiteral( "run-1" ), makePipelineDoc( QStringLiteral( "run-1" ) ).doc );
    RunSnapshotBuilder builder( source );
    auto snapshot = builder.build( QStringLiteral( "run-1" ) );
    REQUIRE( snapshot.has_value() );

    const QJsonObject json = snapshot->toJson();
    auto parsed = RunSnapshot::fromJson( json );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed->toJson() == json );
    REQUIRE( parsed->snapshotDigest() == snapshot->snapshotDigest() );

    // Foreign kind / wrong version are rejected, never reinterpreted.
    QJsonObject foreign = json;
    foreign.insert( QLatin1String( "kind" ), QStringLiteral( "something.else.v9" ) );
    REQUIRE( !RunSnapshot::fromJson( foreign ).has_value() );
    QJsonObject future = json;
    future.insert( QLatin1String( "schema_version" ), kDebuggerSchemaVersion + 3 );
    REQUIRE( !RunSnapshot::fromJson( future ).has_value() );
}

TEST_CASE( "Slice A: oversized evidence fails typed instead of truncating",
           "[debugger][sliceA]" )
{
    StepEvidence big;
    big.mode = StepEvidenceMode::StepsEvidence;
    for ( int i = 0; i <= kMaxSnapshotSteps; ++i )
    {
        StepSnapshot step;
        step.stepId = QStringLiteral( "s%1" ).arg( i );
        step.operatorId = QStringLiteral( "rs:noop" );
        big.steps.append( step );
    }

    InMemoryEvidenceSource source;
    source.insertRun( makeStoredRun( QStringLiteral( "run-1" ) ) );
    source.insertStepEvidence( QStringLiteral( "run-1" ), big );

    RunSnapshotBuilder builder( source );
    auto snapshot = builder.build( QStringLiteral( "run-1" ) );
    REQUIRE( !snapshot.has_value() );
    REQUIRE( snapshot.diagnostics().front().code == QLatin1String( kCodeEvidenceTooLarge ) );
}
