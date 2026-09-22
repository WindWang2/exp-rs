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
#include "experiment/debugger/step_aligner.h"

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

// ============================================================================
// Slice B — deterministic step alignment
// ============================================================================

namespace
{

RunSnapshot snapshotFromEvidence( InMemoryEvidenceSource &source, const QString &runId,
                                  const StepEvidence &evidence )
{
    source.insertRun( makeStoredRun( runId ) );
    source.insertStepEvidence( runId, evidence );
    RunSnapshotBuilder builder( source );
    auto snapshot = builder.build( runId );
    REQUIRE( snapshot.has_value() );
    return snapshot.take();
}

StepEvidence threeStep( const QStringList &ids, const QStringList &operators,
                        const QString &planSignature = QString() )
{
    StepEvidence evidence;
    evidence.mode = StepEvidenceMode::CheckpointSteps;
    evidence.planSignature = planSignature;
    QString previous;
    for ( int i = 0; i < ids.size(); ++i )
    {
        StepSnapshot step;
        step.stepId = ids.at( i );
        step.operatorId = operators.at( i );
        step.status = QStringLiteral( "Completed" );
        if ( !previous.isEmpty() )
            step.dependencies = QStringList{ previous };
        evidence.steps.append( step );
        previous = ids.at( i );
    }
    return evidence;
}

} // namespace

TEST_CASE( "Slice B: same plan signature aligns by step id", "[debugger][sliceB]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    StepEvidence evidence = threeStep( { QStringLiteral( "a" ), QStringLiteral( "b" ), QStringLiteral( "c" ) },
                                       { QStringLiteral( "rs:one" ), QStringLiteral( "rs:two" ), QStringLiteral( "rs:three" ) } );
    evidence.planSignature = QStringLiteral( "plan-777" );
    RunSnapshot refSnap = snapshotFromEvidence( refSource, QStringLiteral( "run-ref" ), evidence );
    RunSnapshot stuSnap = snapshotFromEvidence( stuSource, QStringLiteral( "run-stu" ), evidence );

    auto alignment = StepAligner::align( refSnap, stuSnap );
    REQUIRE( alignment.has_value() );
    REQUIRE( alignment->planRelation == AlignmentResult::PlanRelation::SamePlanSignature );
    REQUIRE( alignment->matches.size() == 3 );
    for ( const StepMatch &match : alignment->matches )
    {
        REQUIRE( match.kind == StepMatch::Kind::ExactId );
        REQUIRE( match.referenceStepId == match.studentStepId );
    }
    REQUIRE( alignment->unmatchedReference.isEmpty() );
    REQUIRE( alignment->unmatchedStudent.isEmpty() );
}

TEST_CASE( "Slice B: different plans align structurally with honest unmatched",
           "[debugger][sliceB]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = snapshotFromEvidence(
        refSource, QStringLiteral( "run-ref" ),
        threeStep( { QStringLiteral( "ndvi" ), QStringLiteral( "threshold" ), QStringLiteral( "area" ) },
                   { QStringLiteral( "rs:ndvi" ), QStringLiteral( "rs:threshold" ), QStringLiteral( "rs:area" ) },
                   QStringLiteral( "plan-reference" ) ) );
    // Student renamed all steps and has no area step (different plan signature).
    RunSnapshot stuSnap = snapshotFromEvidence(
        stuSource, QStringLiteral( "run-stu" ),
        threeStep( { QStringLiteral( "s1" ), QStringLiteral( "s2" ) },
                   { QStringLiteral( "rs:ndvi" ), QStringLiteral( "rs:threshold" ) },
                   QStringLiteral( "plan-student" ) ) );

    auto alignment = StepAligner::align( refSnap, stuSnap );
    REQUIRE( alignment.has_value() );
    REQUIRE( alignment->planRelation == AlignmentResult::PlanRelation::DifferentPlan );
    REQUIRE( alignment->matches.size() == 2 );
    REQUIRE( alignment->matches.at( 0 ).kind == StepMatch::Kind::Structural );
    REQUIRE( alignment->matches.at( 0 ).referenceStepId == QStringLiteral( "ndvi" ) );
    REQUIRE( alignment->matches.at( 0 ).studentStepId == QStringLiteral( "s1" ) );
    REQUIRE( alignment->matches.at( 1 ).referenceStepId == QStringLiteral( "threshold" ) );
    REQUIRE( alignment->matches.at( 1 ).studentStepId == QStringLiteral( "s2" ) );
    REQUIRE( alignment->unmatchedReference == QStringList{ QStringLiteral( "area" ) } );
    REQUIRE( alignment->unmatchedStudent.isEmpty() );
}

TEST_CASE( "Slice B: id equality never overrides operator mismatch",
           "[debugger][sliceB]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = snapshotFromEvidence(
        refSource, QStringLiteral( "run-ref" ),
        threeStep( { QStringLiteral( "step" ) }, { QStringLiteral( "rs:ndvi" ) } ) );
    RunSnapshot stuSnap = snapshotFromEvidence(
        stuSource, QStringLiteral( "run-stu" ),
        threeStep( { QStringLiteral( "step" ) }, { QStringLiteral( "rs:kmeans" ) } ) );

    auto alignment = StepAligner::align( refSnap, stuSnap );
    REQUIRE( alignment.has_value() );
    REQUIRE( alignment->matches.isEmpty() );
    REQUIRE( alignment->unmatchedReference == QStringList{ QStringLiteral( "step" ) } );
    REQUIRE( alignment->unmatchedStudent == QStringList{ QStringLiteral( "step" ) } );
}

TEST_CASE( "Slice B: content-identical output wins candidate choice",
           "[debugger][sliceB]" )
{
    // Two same-operator siblings on the student side; only one produced the
    // same digest as the reference step — that one must be chosen.
    StepEvidence refEvidence;
    refEvidence.mode = StepEvidenceMode::CheckpointSteps;
    StepSnapshot refStep;
    refStep.stepId = QStringLiteral( "stretch" );
    refStep.operatorId = QStringLiteral( "rs:stretch" );
    refStep.status = QStringLiteral( "Completed" );
    refStep.outputDigest = QStringLiteral( "2222" );
    refStep.digestMode = QLatin1String( kDigestModeSha256Hex );
    refEvidence.steps.append( refStep );

    StepEvidence studentEvidence = refEvidence;
    StepSnapshot first = refStep;
    first.stepId = QStringLiteral( "s1" );
    first.outputDigest = QStringLiteral( "1111" );
    StepSnapshot second = refStep;
    second.stepId = QStringLiteral( "s2" );
    second.outputDigest = QStringLiteral( "2222" );
    studentEvidence.steps.clear();
    studentEvidence.steps << first << second;

    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = snapshotFromEvidence( refSource, QStringLiteral( "run-ref" ), refEvidence );
    RunSnapshot stuSnap = snapshotFromEvidence( stuSource, QStringLiteral( "run-stu" ), studentEvidence );

    auto alignment = StepAligner::align( refSnap, stuSnap );
    REQUIRE( alignment.has_value() );
    REQUIRE( alignment->matches.size() == 1 );
    REQUIRE( alignment->matches.front().studentStepId == QStringLiteral( "s2" ) );
    REQUIRE( alignment->unmatchedStudent == QStringList{ QStringLiteral( "s1" ) } );
}

TEST_CASE( "Slice B: partial parent coverage is flagged, not hidden",
           "[debugger][sliceB]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    // Reference: a -> m -> c (c consumes the mask-like producer m).
    StepEvidence refEvidence = threeStep( { QStringLiteral( "a" ), QStringLiteral( "m" ), QStringLiteral( "c" ) },
                                          { QStringLiteral( "rs:one" ), QStringLiteral( "rs:mask" ), QStringLiteral( "rs:three" ) } );
    for ( StepSnapshot &step : refEvidence.steps )
        if ( step.stepId == QStringLiteral( "c" ) )
            step.dependencies = QStringList{ QStringLiteral( "m" ) };
    RunSnapshot refSnap = snapshotFromEvidence( refSource, QStringLiteral( "run-ref" ), refEvidence );

    // Student skipped the mask step: c consumes a directly.
    StepEvidence studentEvidence = threeStep( { QStringLiteral( "a" ), QStringLiteral( "c" ) },
                                              { QStringLiteral( "rs:one" ), QStringLiteral( "rs:three" ) } );
    for ( StepSnapshot &step : studentEvidence.steps )
        if ( step.stepId == QStringLiteral( "c" ) )
            step.dependencies = QStringList{ QStringLiteral( "a" ) };
    RunSnapshot stuSnap = snapshotFromEvidence( stuSource, QStringLiteral( "run-stu" ), studentEvidence );

    auto alignment = StepAligner::align( refSnap, stuSnap );
    REQUIRE( alignment.has_value() );
    REQUIRE( alignment->matches.size() == 2 );
    bool cMatchIncomplete = false;
    for ( const StepMatch &match : alignment->matches )
        if ( match.referenceStepId == QStringLiteral( "c" ) )
            cMatchIncomplete = !match.parentCoverageComplete;
    REQUIRE( cMatchIncomplete );
    REQUIRE( alignment->unmatchedReference == QStringList{ QStringLiteral( "m" ) } );
    REQUIRE( alignment->unmatchedStudent.isEmpty() );
}

TEST_CASE( "Slice B: alignment budget aborts typed instead of running forever",
           "[debugger][sliceB]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = snapshotFromEvidence(
        refSource, QStringLiteral( "run-ref" ),
        threeStep( { QStringLiteral( "a" ), QStringLiteral( "b" ), QStringLiteral( "c" ) },
                   { QStringLiteral( "rs:one" ), QStringLiteral( "rs:one" ), QStringLiteral( "rs:one" ) } ) );
    RunSnapshot stuSnap = snapshotFromEvidence(
        stuSource, QStringLiteral( "run-stu" ),
        threeStep( { QStringLiteral( "x" ), QStringLiteral( "y" ), QStringLiteral( "z" ) },
                   { QStringLiteral( "rs:one" ), QStringLiteral( "rs:one" ), QStringLiteral( "rs:one" ) } ) );

    AlignmentBudget tiny;
    tiny.maxComparisons = 2;
    auto alignment = StepAligner::align( refSnap, stuSnap, tiny );
    REQUIRE( !alignment.has_value() );
    REQUIRE( alignment.diagnostics().front().code ==
             QLatin1String( kCodeAlignmentBudgetExceeded ) );
}

TEST_CASE( "Slice B: alignment is deterministic across repeated runs",
           "[debugger][sliceB]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = snapshotFromEvidence(
        refSource, QStringLiteral( "run-ref" ),
        threeStep( { QStringLiteral( "ndvi" ), QStringLiteral( "threshold" ) },
                   { QStringLiteral( "rs:ndvi" ), QStringLiteral( "rs:threshold" ) } ) );
    RunSnapshot stuSnap = snapshotFromEvidence(
        stuSource, QStringLiteral( "run-stu" ),
        threeStep( { QStringLiteral( "s1" ), QStringLiteral( "s2" ) },
                   { QStringLiteral( "rs:ndvi" ), QStringLiteral( "rs:threshold" ) } ) );
    auto first = StepAligner::align( refSnap, stuSnap );
    auto second = StepAligner::align( refSnap, stuSnap );
    REQUIRE( first.has_value() );
    REQUIRE( second.has_value() );
    REQUIRE( first->toJson() == second->toJson() );
}
