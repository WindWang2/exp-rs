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
#include "experiment/debugger/first_divergence.h"
#include "experiment/debugger/equivalence.h"
#include "catch2/catch_approx.hpp"

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

// ============================================================================
// Slice C — first divergence classification
// ============================================================================

namespace
{

/// Standard checkpoint-mode pipeline evidence: ndvi -> threshold -> area,
/// with parameters on the threshold step (the parameter-bearing step).
StepEvidence checkpointPipeline( const QString &thresholdDigest,
                                 const QJsonObject &thresholdParams,
                                 const QString &thresholdLineage = QStringLiteral( "sig-threshold" ),
                                 bool cacheHit = false,
                                 const QString &areaDigest = QStringLiteral( "cccc" ) )
{
    StepEvidence evidence;
    evidence.mode = StepEvidenceMode::CheckpointSteps;

    StepSnapshot ndvi;
    ndvi.stepId = QStringLiteral( "ndvi" );
    ndvi.operatorId = QStringLiteral( "rs:ndvi" );
    ndvi.lineageSignature = QStringLiteral( "sig-ndvi" );
    ndvi.status = QStringLiteral( "Completed" );
    ndvi.outputDigest = QStringLiteral( "aaaa" );
    ndvi.digestMode = QLatin1String( kDigestModeSha256Hex );
    evidence.steps.append( ndvi );

    StepSnapshot threshold;
    threshold.stepId = QStringLiteral( "threshold" );
    threshold.operatorId = QStringLiteral( "rs:threshold_calc" );
    threshold.parameters = thresholdParams;
    threshold.lineageSignature = thresholdLineage;
    threshold.status = QStringLiteral( "Completed" );
    threshold.outputDigest = thresholdDigest;
    threshold.digestMode = QLatin1String( kDigestModeSha256Hex );
    threshold.cacheHit = cacheHit;
    threshold.cacheHitKnown = true;
    threshold.dependencies = QStringList{ QStringLiteral( "ndvi" ) };
    evidence.steps.append( threshold );

    StepSnapshot area;
    area.stepId = QStringLiteral( "area" );
    area.operatorId = QStringLiteral( "rs:area_stats" );
    area.lineageSignature = QStringLiteral( "sig-area" );
    area.status = QStringLiteral( "Completed" );
    area.outputDigest = areaDigest;
    area.digestMode = QLatin1String( kDigestModeSha256Hex );
    area.dependencies = QStringList{ QStringLiteral( "threshold" ) };
    evidence.steps.append( area );
    return evidence;
}

QJsonObject thresholdParams( double value )
{
    QJsonObject params;
    params.insert( QStringLiteral( "threshold" ), value );
    return params;
}

RunSnapshot buildSnapshot( InMemoryEvidenceSource &source, const QString &runId,
                           const StepEvidence &evidence )
{
    source.insertRun( makeStoredRun( runId ) );
    source.insertStepEvidence( runId, evidence );
    RunSnapshotBuilder builder( source );
    auto snapshot = builder.build( runId );
    REQUIRE( snapshot.has_value() );
    return snapshot.take();
}

} // namespace

TEST_CASE( "Slice C: identical checkpoint pipelines are identical", "[debugger][sliceC]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = buildSnapshot( refSource, QStringLiteral( "run-ref" ),
                                         checkpointPipeline( QStringLiteral( "bbbb" ),
                                                             thresholdParams( 0.35 ) ) );
    RunSnapshot stuSnap = buildSnapshot( stuSource, QStringLiteral( "run-stu" ),
                                         checkpointPipeline( QStringLiteral( "bbbb" ),
                                                             thresholdParams( 0.35 ) ) );
    auto report = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), refSnap, stuSnap );
    REQUIRE( report.has_value() );
    REQUIRE( report->verdict == QStringLiteral( "identical" ) );
    REQUIRE( !report->hasFirstDivergence );
}

TEST_CASE( "Slice C: parameter divergence is located with high confidence",
           "[debugger][sliceC]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = buildSnapshot( refSource, QStringLiteral( "run-ref" ),
                                         checkpointPipeline( QStringLiteral( "bbbb" ),
                                                             thresholdParams( 0.35 ) ) );
    // Physically consistent student run: a different threshold produces a
    // different binary mask AND a different area.
    RunSnapshot stuSnap = buildSnapshot( stuSource, QStringLiteral( "run-stu" ),
                                         checkpointPipeline( QStringLiteral( "eeee" ),
                                                             thresholdParams( 0.62 ),
                                                             QStringLiteral( "sig-threshold" ),
                                                             false,
                                                             QStringLiteral( "dddd" ) ) );
    auto report = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), refSnap, stuSnap );
    REQUIRE( report.has_value() );
    REQUIRE( report->verdict == QStringLiteral( "divergent" ) );
    REQUIRE( report->hasFirstDivergence );
    REQUIRE( report->firstDivergence.kind == DivergenceKind::ParameterDivergence );
    REQUIRE( report->firstDivergence.referenceStepId == QStringLiteral( "threshold" ) );
    REQUIRE( report->firstDivergence.studentStepId == QStringLiteral( "threshold" ) );
    REQUIRE( report->firstDivergence.confidence == CausalConfidence::High );
    // The downstream result divergence is an additional finding, not the first.
    bool sawDownstream = false;
    for ( const DivergenceFinding &finding : report->additionalFindings )
        if ( finding.kind == DivergenceKind::ResultDivergenceWithoutProcessDivergence )
            sawDownstream = true;
    REQUIRE( sawDownstream );
}

TEST_CASE( "Slice C: missing preprocessing is located at the consumer",
           "[debugger][sliceC]" )
{
    // Reference: ndvi -> mask -> threshold -> area. Student skips the mask.
    auto pipelineWithMask = []( const QString &maskDigest ) {
        StepEvidence evidence = checkpointPipeline( QStringLiteral( "bbbb" ),
                                                    thresholdParams( 0.35 ) );
        StepSnapshot mask;
        mask.stepId = QStringLiteral( "mask" );
        mask.operatorId = QStringLiteral( "rs:mask" );
        mask.lineageSignature = QStringLiteral( "sig-mask" );
        mask.status = QStringLiteral( "Completed" );
        mask.outputDigest = maskDigest;
        mask.digestMode = QLatin1String( kDigestModeSha256Hex );
        mask.dependencies = QStringList{ QStringLiteral( "ndvi" ) };
        evidence.steps.insert( 1, mask );
        for ( StepSnapshot &step : evidence.steps )
            if ( step.stepId == QStringLiteral( "threshold" ) )
                step.dependencies = QStringList{ QStringLiteral( "mask" ) };
        return evidence;
    };

    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = buildSnapshot( refSource, QStringLiteral( "run-ref" ),
                                         pipelineWithMask( QStringLiteral( "ffff" ) ) );
    // Physically consistent: without the mask, the student's threshold step
    // produces a DIFFERENT binary mask (even at the same threshold value).
    RunSnapshot stuSnap = buildSnapshot( stuSource, QStringLiteral( "run-stu" ),
                                         checkpointPipeline( QStringLiteral( "eeee" ),
                                                             thresholdParams( 0.35 ) ) );
    auto report = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), refSnap, stuSnap );
    REQUIRE( report.has_value() );
    REQUIRE( report->verdict == QStringLiteral( "divergent" ) );
    REQUIRE( report->firstDivergence.kind == DivergenceKind::MissingPreprocessing );
    // Located at the matched consumer of the missing producer.
    REQUIRE( report->firstDivergence.referenceStepId == QStringLiteral( "threshold" ) );
    REQUIRE( report->firstDivergence.studentStepId == QStringLiteral( "threshold" ) );
    REQUIRE( report->firstDivergence.confidence == CausalConfidence::High );
}

TEST_CASE( "Slice C: different raw input state is located before any step",
           "[debugger][sliceC]" )
{
    // Same pipeline, different root-input fingerprint (provenance mode).
    auto doc = []( const QString &runId, const QString &rootDigest ) {
        return makeProvenanceDoc(
            runId, QStringLiteral( "plan-1" ),
            {
                { QStringLiteral( "ndvi" ), QStringLiteral( "rs:ndvi" ), QStringLiteral( "Succeeded" ),
                  QStringLiteral( "sig-ndvi" ), false,
                  QStringLiteral( "/lab/%1/ndvi.tif" ).arg( runId ),
                  QStringLiteral( "sha256fl:aa11" ), 2048 },
            },
            {
                { QStringLiteral( "ndvi" ), QStringLiteral( "/data/raw/scene.tif" ),
                  rootDigest, 999999 },
            } );
    };

    InMemoryEvidenceSource refSource, stuSource;
    refSource.insertRun( makeStoredRun( QStringLiteral( "run-ref" ) ) );
    refSource.insertProvenanceDoc( QStringLiteral( "run-ref" ),
                                   doc( QStringLiteral( "run-ref" ),
                                        QStringLiteral( "sha256fl:root1" ) ) );
    stuSource.insertRun( makeStoredRun( QStringLiteral( "run-stu" ) ) );
    stuSource.insertProvenanceDoc( QStringLiteral( "run-stu" ),
                                   doc( QStringLiteral( "run-stu" ),
                                        QStringLiteral( "sha256fl:root2" ) ) );

    RunSnapshotBuilder refBuilder( refSource );
    RunSnapshotBuilder stuBuilder( stuSource );
    auto refSnap = refBuilder.build( QStringLiteral( "run-ref" ) );
    auto stuSnap = stuBuilder.build( QStringLiteral( "run-stu" ) );
    REQUIRE( refSnap.has_value() );
    REQUIRE( stuSnap.has_value() );

    auto report = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), *refSnap, *stuSnap );
    REQUIRE( report.has_value() );
    REQUIRE( report->verdict == QStringLiteral( "divergent" ) );
    REQUIRE( report->firstDivergence.kind == DivergenceKind::DifferentInputState );
    REQUIRE( report->firstDivergence.referenceStepId == QStringLiteral( "ndvi" ) );
    REQUIRE( report->firstDivergence.confidence == CausalConfidence::High );
}

TEST_CASE( "Slice C: dataset identity difference is non-comparable, no step walk",
           "[debugger][sliceC]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = buildSnapshot( refSource, QStringLiteral( "run-ref" ),
                                         checkpointPipeline( QStringLiteral( "bbbb" ),
                                                             thresholdParams( 0.35 ) ) );
    RunSnapshot stuSnap = buildSnapshot( stuSource, QStringLiteral( "run-stu" ),
                                         checkpointPipeline( QStringLiteral( "bbbb" ),
                                                             thresholdParams( 0.35 ) ) );
    // Different dataset identity pins at run level.
    ExperimentRun refRun = makeStoredRun( QStringLiteral( "run-ref" ) );
    ExperimentRun stuRun = makeStoredRun( QStringLiteral( "run-stu" ) );
    stuRun.setDatasetVersionId( QStringLiteral( "dsv-OTHER" ) );
    stuRun.setDatasetFingerprint( QStringLiteral( "fp-dataset-OTHER" ) );

    auto report = FirstDivergenceAnalyzer::analyze( refRun, stuRun, refSnap, stuSnap );
    REQUIRE( report.has_value() );
    REQUIRE( report->verdict == QStringLiteral( "non_comparable" ) );
    REQUIRE( report->hasFirstDivergence );
    REQUIRE( report->firstDivergence.kind == DivergenceKind::DataSubsetDivergence );
    REQUIRE( report->firstDivergence.confidence == CausalConfidence::High );
    // No step walk happened.
    REQUIRE( report->alignment.isEmpty() );
    REQUIRE( !report->evidenceGaps.isEmpty() );
}

TEST_CASE( "Slice C: absent step evidence degrades honestly to incomplete",
           "[debugger][sliceC]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    refSource.insertRun( makeStoredRun( QStringLiteral( "run-ref" ) ) );
    stuSource.insertRun( makeStoredRun( QStringLiteral( "run-stu" ) ) );
    RunSnapshotBuilder refBuilder( refSource );
    RunSnapshotBuilder stuBuilder( stuSource );
    auto refSnap = refBuilder.build( QStringLiteral( "run-ref" ) );
    auto stuSnap = stuBuilder.build( QStringLiteral( "run-stu" ) );
    REQUIRE( refSnap.has_value() );
    REQUIRE( stuSnap.has_value() );

    auto report = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), *refSnap, *stuSnap );
    REQUIRE( report.has_value() );
    REQUIRE( report->verdict == QStringLiteral( "incomplete" ) );
    REQUIRE( !report->hasFirstDivergence );
    REQUIRE( report->evidenceGaps.size() == 2 );
}

TEST_CASE( "Slice C: result divergence without process divergence, honest confidence",
           "[debugger][sliceC]" )
{
    // Same params, same lineage, different output digest, same digest mode.
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = buildSnapshot( refSource, QStringLiteral( "run-ref" ),
                                         checkpointPipeline( QStringLiteral( "bbbb" ),
                                                             thresholdParams( 0.35 ) ) );
    RunSnapshot stuSnap = buildSnapshot( stuSource, QStringLiteral( "run-stu" ),
                                         checkpointPipeline( QStringLiteral( "ZZZZ" ),
                                                             thresholdParams( 0.35 ) ) );
    auto report = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), refSnap, stuSnap );
    REQUIRE( report.has_value() );
    REQUIRE( report->firstDivergence.kind ==
             DivergenceKind::ResultDivergenceWithoutProcessDivergence );
    REQUIRE( report->firstDivergence.referenceStepId == QStringLiteral( "threshold" ) );

    // Same process identity but one side was served from cache: confidence
    // drops to medium — the nondeterminism suspect list grows.
    InMemoryEvidenceSource cacheSource;
    RunSnapshot cacheSnap = buildSnapshot(
        cacheSource, QStringLiteral( "run-cache" ),
        checkpointPipeline( QStringLiteral( "ZZZZ" ), thresholdParams( 0.35 ),
                            QStringLiteral( "sig-threshold" ), /*cacheHit=*/true ) );
    auto cacheReport = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-cache" ) ), refSnap, cacheSnap );
    REQUIRE( cacheReport.has_value() );
    REQUIRE( cacheReport->firstDivergence.kind ==
             DivergenceKind::ResultDivergenceWithoutProcessDivergence );
    REQUIRE( cacheReport->firstDivergence.confidence == CausalConfidence::Medium );
}

TEST_CASE( "Slice C: mixed digest modes are unknown, never guessed",
           "[debugger][sliceC]" )
{
    // fast vs full workflow fingerprints on identical process identity.
    auto fastPipeline = []( const QString &runId, const char *digest,
                            const char *mode = "sha256fl" ) {
        StepEvidence evidence;
        evidence.mode = StepEvidenceMode::CheckpointSteps;
        StepSnapshot step;
        step.stepId = QStringLiteral( "calibrate" );
        step.operatorId = QStringLiteral( "rs:calibrate" );
        step.lineageSignature = QStringLiteral( "sig-cal" );
        step.status = QStringLiteral( "Completed" );
        step.outputDigest = QString::fromLatin1( digest );
        step.digestMode = QString::fromLatin1( mode );
        evidence.steps.append( step );
        return evidence;
    };

    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = buildSnapshot( refSource, QStringLiteral( "run-ref" ),
                                         fastPipeline( QStringLiteral( "run-ref" ),
                                                       "sha256fl:11" ) );
    RunSnapshot stuSnap = buildSnapshot( stuSource, QStringLiteral( "run-stu" ),
                                         fastPipeline( QStringLiteral( "run-stu" ),
                                                       "sha256full:22", "sha256full" ) );
    auto report = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), refSnap, stuSnap );
    REQUIRE( report.has_value() );
    REQUIRE( report->verdict == QStringLiteral( "divergent" ) );
    REQUIRE( report->firstDivergence.kind == DivergenceKind::UnknownNonComparable );
    REQUIRE( report->firstDivergence.confidence == CausalConfidence::None );
    REQUIRE( !report->firstDivergence.missingEvidence.isEmpty() );
}

TEST_CASE( "Slice C: student-only terminal step keeps the verdict equivalent",
           "[debugger][sliceC]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = buildSnapshot( refSource, QStringLiteral( "run-ref" ),
                                         checkpointPipeline( QStringLiteral( "bbbb" ),
                                                             thresholdParams( 0.35 ) ) );
    StepEvidence student = checkpointPipeline( QStringLiteral( "bbbb" ),
                                               thresholdParams( 0.35 ) );
    StepSnapshot extra;
    extra.stepId = QStringLiteral( "histogram" );
    extra.operatorId = QStringLiteral( "rs:histogram" );
    extra.status = QStringLiteral( "Completed" );
    extra.outputDigest = QStringLiteral( "dddd" );
    extra.digestMode = QLatin1String( kDigestModeSha256Hex );
    student.steps.append( extra );
    RunSnapshot stuSnap = buildSnapshot( stuSource, QStringLiteral( "run-stu" ), student );

    auto report = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), refSnap, stuSnap );
    REQUIRE( report.has_value() );
    REQUIRE( report->verdict == QStringLiteral( "equivalent" ) );
    REQUIRE( !report->hasFirstDivergence );
    REQUIRE( report->additionalFindings.size() == 1 );
    REQUIRE( report->additionalFindings.front().kind ==
             DivergenceKind::EquivalentAlternativePath );
    REQUIRE( report->additionalFindings.front().studentStepId ==
             QStringLiteral( "histogram" ) );
}

TEST_CASE( "Slice C: analysis is deterministic across repeated runs",
           "[debugger][sliceC]" )
{
    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = buildSnapshot( refSource, QStringLiteral( "run-ref" ),
                                         checkpointPipeline( QStringLiteral( "bbbb" ),
                                                             thresholdParams( 0.35 ) ) );
    RunSnapshot stuSnap = buildSnapshot( stuSource, QStringLiteral( "run-stu" ),
                                         checkpointPipeline( QStringLiteral( "eeee" ),
                                                             thresholdParams( 0.62 ) ) );
    auto first = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), refSnap, stuSnap );
    auto second = FirstDivergenceAnalyzer::analyze(
        makeStoredRun( QStringLiteral( "run-ref" ) ),
        makeStoredRun( QStringLiteral( "run-stu" ) ), refSnap, stuSnap );
    REQUIRE( first.has_value() );
    REQUIRE( second.has_value() );
    REQUIRE( first->toJson() == second->toJson() );
}

// ============================================================================
// Slice D — equivalence profiles + invariant references
// ============================================================================

namespace
{

EquivalenceProfile profileWithTolerance()
{
    EquivalenceProfile profile;
    profile.profileId = QStringLiteral( "lab-tolerances" );
    EquivalenceProfile::Rule tolerance;
    tolerance.kind = EquivalenceProfile::RuleKind::ParamTolerance;
    tolerance.ruleId = QStringLiteral( "threshold-window" );
    tolerance.op = QStringLiteral( "rs:threshold_calc" );
    tolerance.keys = QStringList{ QStringLiteral( "threshold" ) };
    tolerance.tolerance = 0.05;
    profile.rules.append( tolerance );
    return profile;
}

} // namespace

TEST_CASE( "Slice D: operator group rule accepts a different but declared-equal operator",
           "[debugger][sliceD]" )
{
    EquivalenceProfile profile;
    profile.profileId = QStringLiteral( "prep" );
    EquivalenceProfile::Rule group;
    group.kind = EquivalenceProfile::RuleKind::OperatorGroup;
    group.ruleId = QStringLiteral( "stretch-group" );
    group.operators = QStringList{ QStringLiteral( "rs:stretch_linear" ),
                                   QStringLiteral( "rs:histogram_equalize" ) };
    profile.rules.append( group );

    StepEvidence refEvidence = threeStep( { QStringLiteral( "prep" ), QStringLiteral( "classify" ) },
                                          { QStringLiteral( "rs:stretch_linear" ), QStringLiteral( "rs:classify" ) } );
    StepEvidence studentEvidence = threeStep( { QStringLiteral( "prep" ), QStringLiteral( "classify" ) },
                                              { QStringLiteral( "rs:histogram_equalize" ), QStringLiteral( "rs:classify" ) } );
    studentEvidence.planSignature = QStringLiteral( "other-plan" );

    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = snapshotFromEvidence( refSource, QStringLiteral( "run-ref" ), refEvidence );
    RunSnapshot stuSnap = snapshotFromEvidence( stuSource, QStringLiteral( "run-stu" ), studentEvidence );

    auto alignment = StepAligner::align( refSnap, stuSnap, profile );
    REQUIRE( alignment.has_value() );
    REQUIRE( alignment->matches.size() == 2 );
    REQUIRE( alignment->matches.front().kind == StepMatch::Kind::EquivalentRule );
    REQUIRE( alignment->matches.front().equivalenceRuleId == QStringLiteral( "stretch-group" ) );

    ExperimentRun refRun = makeStoredRun( QStringLiteral( "run-ref" ) );
    ExperimentRun stuRun = makeStoredRun( QStringLiteral( "run-stu" ) );
    auto withProfile = FirstDivergenceAnalyzer::analyze( refRun, stuRun, refSnap, stuSnap, profile );
    REQUIRE( withProfile.has_value() );
    // The accepted match never masks reality: it is recorded as an
    // alternative-path finding naming the rule, and with nothing else
    // differing the verdict is "equivalent" (firstDivergence stays empty).
    REQUIRE( withProfile->verdict == QStringLiteral( "equivalent" ) );
    REQUIRE( !withProfile->hasFirstDivergence );
    bool ruleRecorded = false;
    for ( const DivergenceFinding &finding : withProfile->additionalFindings )
        if ( finding.kind == DivergenceKind::EquivalentAlternativePath
             && finding.equivalenceRuleId == QStringLiteral( "stretch-group" ) )
            ruleRecorded = true;
    REQUIRE( ruleRecorded );
}

TEST_CASE( "Slice D: without a profile the same difference is a real divergence",
           "[debugger][sliceD]" )
{
    // Same evidence as above, NO profile — nothing silently accepts it.
    InMemoryEvidenceSource refSource, stuSource;
    StepEvidence refEvidence = threeStep( { QStringLiteral( "prep" ), QStringLiteral( "classify" ) },
                                          { QStringLiteral( "rs:stretch_linear" ), QStringLiteral( "rs:classify" ) } );
    StepEvidence studentEvidence = threeStep( { QStringLiteral( "prep" ), QStringLiteral( "classify" ) },
                                              { QStringLiteral( "rs:histogram_equalize" ), QStringLiteral( "rs:classify" ) } );
    studentEvidence.planSignature = QStringLiteral( "other-plan" );
    RunSnapshot refSnap = snapshotFromEvidence( refSource, QStringLiteral( "run-ref" ), refEvidence );
    RunSnapshot stuSnap = snapshotFromEvidence( stuSource, QStringLiteral( "run-stu" ), studentEvidence );

    auto alignment = StepAligner::align( refSnap, stuSnap );
    REQUIRE( alignment.has_value() );
    // The classify step still matches (same operator); the prep step does not.
    REQUIRE( alignment->matches.size() == 1 );
    REQUIRE( alignment->matches.front().referenceStepId == QStringLiteral( "classify" ) );
    REQUIRE( alignment->unmatchedReference.contains( QStringLiteral( "prep" ) ) );
    REQUIRE( alignment->unmatchedStudent.contains( QStringLiteral( "prep" ) ) );
}

TEST_CASE( "Slice D: parameter tolerance accepts within-window and reports outside-window",
           "[debugger][sliceD]" )
{
    EquivalenceProfile profile = profileWithTolerance();

    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = buildSnapshot( refSource, QStringLiteral( "run-ref" ),
                                         checkpointPipeline( QStringLiteral( "bbbb" ),
                                                             thresholdParams( 0.35 ) ) );
    // Within the 0.05 window: accepted despite a different params hash.
    RunSnapshot nearSnap = buildSnapshot( stuSource, QStringLiteral( "run-near" ),
                                          checkpointPipeline( QStringLiteral( "eeee" ),
                                                              thresholdParams( 0.38 ) ) );
    // Outside the window: reported as a real parameter divergence.
    RunSnapshot farSnap = buildSnapshot( stuSource, QStringLiteral( "run-far" ),
                                         checkpointPipeline( QStringLiteral( "ffff" ),
                                                             thresholdParams( 0.62 ) ) );

    ExperimentRun refRun = makeStoredRun( QStringLiteral( "run-ref" ) );
    ExperimentRun nearRun = makeStoredRun( QStringLiteral( "run-near" ) );
    ExperimentRun farRun = makeStoredRun( QStringLiteral( "run-far" ) );

    auto accepted = FirstDivergenceAnalyzer::analyze( refRun, nearRun, refSnap, nearSnap, profile );
    REQUIRE( accepted.has_value() );
    // A within-window difference is declared equivalent by the profile; the
    // acceptance is recorded as a named finding, not silently swallowed.
    REQUIRE( accepted->verdict == QStringLiteral( "equivalent" ) );
    REQUIRE( !accepted->hasFirstDivergence );
    bool toleranceRecorded = false;
    for ( const DivergenceFinding &finding : accepted->additionalFindings )
        if ( finding.kind == DivergenceKind::EquivalentAlternativePath
             && finding.equivalenceRuleId == QStringLiteral( "threshold-window" ) )
            toleranceRecorded = true;
    REQUIRE( toleranceRecorded );

    auto rejected = FirstDivergenceAnalyzer::analyze( refRun, farRun, refSnap, farSnap, profile );
    REQUIRE( rejected.has_value() );
    REQUIRE( rejected->firstDivergence.kind == DivergenceKind::ParameterDivergence );
}

TEST_CASE( "Slice D: geometry-key differences classify as geometry divergence",
           "[debugger][sliceD]" )
{
    EquivalenceProfile profile;
    profile.profileId = QStringLiteral( "geometry" );
    EquivalenceProfile::Rule geometry;
    geometry.kind = EquivalenceProfile::RuleKind::GeometryKeys;
    geometry.ruleId = QStringLiteral( "warp-geometry" );
    geometry.op = QStringLiteral( "rs:warp" );
    geometry.keys = QStringList{ QStringLiteral( "target_crs" ), QStringLiteral( "resampling" ) };
    profile.rules.append( geometry );

    QJsonObject refParams;
    refParams.insert( QStringLiteral( "target_crs" ), QStringLiteral( "EPSG:4326" ) );
    refParams.insert( QStringLiteral( "resampling" ), QStringLiteral( "nearest" ) );
    QJsonObject studentParams;
    studentParams.insert( QStringLiteral( "target_crs" ), QStringLiteral( "EPSG:32649" ) );
    studentParams.insert( QStringLiteral( "resampling" ), QStringLiteral( "bilinear" ) );

    StepEvidence refEvidence = threeStep( { QStringLiteral( "warp" ) }, { QStringLiteral( "rs:warp" ) } );
    refEvidence.steps.first().parameters = refParams;
    StepEvidence studentEvidence = threeStep( { QStringLiteral( "warp" ) }, { QStringLiteral( "rs:warp" ) } );
    studentEvidence.steps.first().parameters = studentParams;
    studentEvidence.planSignature = QStringLiteral( "other-plan" );

    InMemoryEvidenceSource refSource, stuSource;
    RunSnapshot refSnap = snapshotFromEvidence( refSource, QStringLiteral( "run-ref" ), refEvidence );
    RunSnapshot stuSnap = snapshotFromEvidence( stuSource, QStringLiteral( "run-stu" ), studentEvidence );

    ExperimentRun refRun = makeStoredRun( QStringLiteral( "run-ref" ) );
    ExperimentRun stuRun = makeStoredRun( QStringLiteral( "run-stu" ) );
    auto report = FirstDivergenceAnalyzer::analyze( refRun, stuRun, refSnap, stuSnap, profile );
    REQUIRE( report.has_value() );
    REQUIRE( report->firstDivergence.kind == DivergenceKind::GeometryAlignmentDivergence );
}

TEST_CASE( "Slice D: profile documents are strict and versioned",
           "[debugger][sliceD]" )
{
    EquivalenceProfile profile = profileWithTolerance();
    auto parsed = EquivalenceProfile::fromJson( profile.toJson() );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed->toJson() == profile.toJson() );

    QJsonObject foreign = profile.toJson();
    foreign.insert( QLatin1String( "kind" ), QStringLiteral( "wrong.kind" ) );
    REQUIRE( !EquivalenceProfile::fromJson( foreign ).has_value() );

    // Duplicate rule ids are rejected.
    EquivalenceProfile duplicate = profileWithTolerance();
    duplicate.rules.append( duplicate.rules.front() );
    REQUIRE( !EquivalenceProfile::fromJson( duplicate.toJson() ).has_value() );
}

TEST_CASE( "Slice D: invariant reference evaluates honestly, gaps are named",
           "[debugger][sliceD]" )
{
    QVector<Invariant> invariants;
    Invariant metricOk;
    metricOk.kind = Invariant::Kind::MetricWithin;
    metricOk.invariantId = QStringLiteral( "area-sane" );
    metricOk.metricPath = QStringLiteral( "area_km2" );
    metricOk.minValue = 0.0;
    metricOk.maxValue = 100.0;
    invariants.append( metricOk );

    Invariant noForbidden;
    noForbidden.kind = Invariant::Kind::NoStepOfOperator;
    noForbidden.invariantId = QStringLiteral( "no-direct-classify" );
    noForbidden.operatorId = QStringLiteral( "rs:supervised_classification" );
    invariants.append( noForbidden );

    Invariant enoughSteps;
    enoughSteps.kind = Invariant::Kind::StepCountAtLeast;
    enoughSteps.invariantId = QStringLiteral( "full-pipeline" );
    enoughSteps.stepCount = 3;
    invariants.append( enoughSteps );

    InMemoryEvidenceSource source;
    RunSnapshot snapshot = buildSnapshot( source, QStringLiteral( "run-stu" ),
                                          checkpointPipeline( QStringLiteral( "bbbb" ),
                                                              thresholdParams( 0.35 ) ) );
    QJsonObject metrics;
    metrics.insert( QStringLiteral( "area_km2" ), 42.5 );
    snapshot.setMetrics( metrics );

    auto checks = evaluateInvariants( invariants, snapshot );
    REQUIRE( checks.size() == 3 );
    REQUIRE( checks.at( 0 ).passed );
    REQUIRE( checks.at( 1 ).passed );
    REQUIRE( checks.at( 2 ).passed );

    // Missing metric → unevaluable gap, never a silent pass. (Explicit copy:
    // QVector::operator<< mutates in place and would pollute the later
    // analyzeAgainstInvariants assertions.)
    Invariant missingMetric = metricOk;
    missingMetric.invariantId = QStringLiteral( "kappa-sane" );
    missingMetric.metricPath = QStringLiteral( "overall_accuracy" );
    QVector<Invariant> extended = invariants;
    extended.append( missingMetric );
    auto withGap = evaluateInvariants( extended, snapshot );
    REQUIRE( withGap.size() == 4 );
    REQUIRE( !withGap.last().evaluable );
    REQUIRE( !withGap.last().passed );

    auto report = analyzeAgainstInvariants( snapshot, invariants );
    REQUIRE( report.has_value() );
    REQUIRE( report->verdict == QStringLiteral( "equivalent" ) );

    QJsonObject badMetrics;
    badMetrics.insert( QStringLiteral( "area_km2" ), 4200.0 );
    snapshot.setMetrics( badMetrics );
    auto failing = analyzeAgainstInvariants( snapshot, invariants );
    REQUIRE( failing.has_value() );
    REQUIRE( failing->verdict == QStringLiteral( "divergent" ) );
    REQUIRE( failing->hasFirstDivergence );
    REQUIRE( failing->firstDivergence.evidence.front().contains( QStringLiteral( "area-sane" ) ) );
}

