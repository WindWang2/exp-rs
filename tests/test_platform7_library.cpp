// test_platform7_library.cpp — Platform 7.0 library-level tests:
//   - sample promotion (goal §B): classification/segmentation/annotation/
//     pair/temporal promotion with class-identity and immutability contracts
//   - fold audit (goal §D): per-fold leakage, zero-ratio flags, replay
//   - facets & quality cache (goal §E): bounded distributions, staleness
//   - replay readiness (goal §F): honest levels, missing diagnostics,
//     equivalent-run lookup
//   - comparison extensions (goal §G): protocol/schema compatibility, paired
//     honesty
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/fold_audit.h"
#include "dataset/label_schema.h"
#include "dataset/leakage_audit.h"
#include "dataset/sample_promotion.h"
#include "dataset/split.h"
#include "experiment/comparison_ext.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/replay_readiness.h"
#include "experiment/run_recorder.h"

#include <QFileInfo>
#include <cmath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

LabelSchema twoClassSchema()
{
    LabelSchema schema;
    schema.setSchemaId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    schema.setVersion( 1 );
    LabelClass water;
    water.setStableId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    water.setCode( QStringLiteral( "water" ) );
    LabelClass forest;
    forest.setStableId( QStringLiteral( "33333333-3333-4333-8333-333333333333" ) );
    forest.setCode( QStringLiteral( "forest" ) );
    schema.classes().append( water );
    schema.classes().append( forest );
    REQUIRE( schema.validate().has_value() );
    return schema;
}

/// A draft version with @p count samples (groupId group-a/b, provenance
/// content_digest) ready for promotion/facet/fold experiments.
struct DraftFixture
{
    QTemporaryDir dir;
    DatasetStore store;
    DatasetId datasetId;
    DatasetVersionId versionId;
    QVector<SampleRecord> samples;

    explicit DraftFixture( int count = 8 )
    {
        REQUIRE( store.open( dir.filePath( QStringLiteral( "ds.db" ) ) ) );
        datasetId = DatasetId::generate();
        REQUIRE( store.createDataset( datasetId, QStringLiteral( "p7" ) ).has_value() );
        versionId = DatasetVersionId::generate();
        DatasetManifest manifest;
        manifest.setDatasetId( datasetId.toString() );
        manifest.setVersionId( versionId.toString() );
        manifest.setName( QStringLiteral( "p7" ) );
        manifest.setCreatedAtUtc( QDateTime::fromString(
            QStringLiteral( "2026-09-09T00:00:00.000Z" ), Qt::ISODateWithMs ) );
        REQUIRE( store.createDraftVersion( manifest ).has_value() );

        for ( int i = 0; i < count; ++i )
        {
            SampleRecord sample;
            sample.setSampleId( SampleId::generate().toString() );
            sample.setDatasetVersionId( versionId.toString() );
            sample.setKind( SampleKind::Point );
            sample.setGroupId( i % 2 == 0 ? QStringLiteral( "group-a" ) : QStringLiteral( "group-b" ) );
            PointSample point;
            point.x = i;
            point.y = i;
            sample.payload() = point;
            sample.provenance()[QStringLiteral( "content_digest" )] =
                QStringLiteral( "dg-%1" ).arg( i % 3 );
            samples.append( sample );
        }
        REQUIRE( store.addSamples( samples ).has_value() );
    }

    PromotionSource source() const
    {
        PromotionSource s;
        s.assetId = QStringLiteral( "aaaaaaaa-0000-4000-8000-000000000009" );
        s.revision = 1;
        s.annotationSource = AnnotationSourceType::ModelAssisted;
        s.sourceDetail = QJsonObject{
            { QStringLiteral( "model_id" ), QStringLiteral( "segformer" ) },
            { QStringLiteral( "model_digest" ), QStringLiteral( "deadbeef" ) },
            { QStringLiteral( "threshold" ), 0.5 },
        };
        s.workflowProvenance =
            QJsonObject{ { QStringLiteral( "operator" ), QStringLiteral( "rs:classify" ) } };
        return s;
    }
};

} // namespace

// --- M2: promotion -------------------------------------------------------------

TEST_CASE( "classification promotion maps raw values to class codes",
           "[promotion][classification]" )
{
    DraftFixture fx;
    SamplePromoter promoter( fx.store );
    const auto schema = twoClassSchema();

    QVector<ClassifiedRegion> regions;
    ClassifiedRegion a;
    a.rawValue = 7;
    a.geometryWkt = QStringLiteral( "POLYGON((0 0, 4 0, 4 4, 0 4, 0 0))" );
    a.groupId = QStringLiteral( "scene-1" );
    regions.append( a );
    ClassifiedRegion b;
    b.rawValue = 9;
    b.geometryWkt = QStringLiteral( "POLYGON((10 10, 14 10, 14 14, 10 14, 10 10))" );
    regions.append( b );

    // raw values {7, 9} happen to be row indices of some cv::Mat — the RULES
    // decouple them from identity: a permutation of the mapping changes which
    // codes are recorded while raw values stay invisible.
    QVector<ClassCodeRule> rules = {
        { 7, QStringLiteral( "water" ) },
        { 9, QStringLiteral( "forest" ) },
    };
    const auto promoted =
        promoter.promoteClassification( fx.versionId, fx.source(), rules, regions, &schema );
    REQUIRE( promoted.has_value() );
    CHECK( promoted.value().samplesWritten == 2 );
    CHECK( promoted.value().annotationsWritten == 2 );

    const auto page = fx.store.samplesPage( fx.versionId, 0, 500 );
    REQUIRE( page.has_value() );
    qint64 polygonSamples = 0;
    QStringList promotedCodes;
    for ( const auto &sample : page.value().second )
    {
        if ( sample.kind() != SampleKind::Polygon )
            continue;
        ++polygonSamples;
        const auto tips = fx.store.annotationsOfSample( sample.sampleId() );
        REQUIRE( tips.size() == 1 );
        promotedCodes.append( tips.first().classCode() );
        // Provenance is mandatory: producing asset + source type.
        CHECK( sample.provenance().value( QStringLiteral( "producing_asset" ) )
                   .toObject()
                   .value( QStringLiteral( "asset_id" ) )
                   .toString()
                   == fx.source().assetId );
        CHECK( sample.provenance().value( QStringLiteral( "label_source_type" ) ).toString() ==
               QStringLiteral( "model_assisted" ) );
    }
    CHECK( polygonSamples == 2 );
    CHECK( promotedCodes.contains( QStringLiteral( "water" ) ) );
    CHECK( promotedCodes.contains( QStringLiteral( "forest" ) ) );
}

TEST_CASE( "promotion refuses unmapped raw values and unknown classes",
           "[promotion][classification]" )
{
    DraftFixture fx;
    SamplePromoter promoter( fx.store );
    const auto schema = twoClassSchema();

    QVector<ClassifiedRegion> regions;
    ClassifiedRegion known;
    known.rawValue = 1;
    known.geometryWkt = QStringLiteral( "POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))" );
    regions.append( known );
    ClassifiedRegion unmapped;
    unmapped.rawValue = 42;
    unmapped.geometryWkt = QStringLiteral( "POLYGON((2 2, 3 2, 3 3, 2 3, 2 2))" );
    regions.append( unmapped );

    QVector<ClassCodeRule> rules = { { 1, QStringLiteral( "water" ) } };
    const auto refused =
        promoter.promoteClassification( fx.versionId, fx.source(), rules, regions, &schema );
    REQUIRE( !refused.has_value() );
    CHECK( refused.diagnostics().first().code == QStringLiteral( "dataset.promotion_unmapped_values" ) );
    CHECK( refused.diagnostics().first().message.contains( QStringLiteral( "42" ) ) );

    // Unknown class code under the schema.
    QVector<ClassCodeRule> badRules = { { 1, QStringLiteral( "not-a-class" ) } };
    const auto unknown =
        promoter.promoteClassification( fx.versionId, fx.source(), badRules, { known }, &schema );
    REQUIRE( !unknown.has_value() );
    CHECK( unknown.diagnostics().first().code == QStringLiteral( "dataset.promotion_unknown_class" ) );
}

TEST_CASE( "promotion writes only into draft versions", "[promotion][immutability]" )
{
    DraftFixture fx;
    SamplePromoter promoter( fx.store );
    const auto schema = twoClassSchema();
    REQUIRE( fx.store.stageVersion( fx.versionId ).has_value() );
    REQUIRE( fx.store.commitVersion( fx.versionId ).has_value() );

    QVector<ClassifiedRegion> regions;
    ClassifiedRegion region;
    region.rawValue = 1;
    region.geometryWkt = QStringLiteral( "POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))" );
    regions.append( region );
    const auto refused = promoter.promoteClassification(
        fx.versionId, fx.source(), { { 1, QStringLiteral( "water" ) } }, regions, &schema );
    REQUIRE( !refused.has_value() );
    CHECK( refused.diagnostics().first().code == QStringLiteral( "dataset.not_draft" ) );
}

TEST_CASE( "segmentation and annotation promotion record provenance",
           "[promotion][segmentation][annotation]" )
{
    DraftFixture fx;
    SamplePromoter promoter( fx.store );
    const auto schema = twoClassSchema();

    QVector<SegmentationObject> objects;
    SegmentationObject object;
    object.objectRef = QStringLiteral( "seg-0001" );
    object.classCode = QStringLiteral( "forest" );
    object.bounds = PixelWindow{ 10, 10, 64, 64 };
    objects.append( object );
    const auto promoted =
        promoter.promoteSegmentation( fx.versionId, fx.source(), objects, &schema );
    REQUIRE( promoted.has_value() );
    CHECK( promoted.value().samplesWritten == 1 );
    CHECK( promoted.value().annotationsWritten == 1 );

    // Annotation promotion onto an EXISTING sample.
    const auto existingId = fx.samples.first().sampleId();
    QVector<AnnotationPromotion> annotations;
    AnnotationPromotion annotation;
    annotation.targetSampleId = existingId;
    annotation.classCode = QStringLiteral( "water" );
    annotations.append( annotation );
    const auto annotated = promoter.promoteAnnotations( fx.versionId, fx.source(), annotations, &schema );
    REQUIRE( annotated.has_value() );
    CHECK( annotated.value().annotationsWritten == 1 );

    // Annotation onto a sample that is not in the version is refused.
    AnnotationPromotion ghost;
    ghost.targetSampleId = SampleId::generate().toString();
    ghost.classCode = QStringLiteral( "water" );
    const auto missing = promoter.promoteAnnotations( fx.versionId, fx.source(), { ghost }, &schema );
    REQUIRE( !missing.has_value() );
    CHECK( missing.diagnostics().first().code == QStringLiteral( "dataset.sample_not_found" ) );
}

TEST_CASE( "pairs require event groups; temporal keeps missing observations",
           "[promotion][pair][temporal]" )
{
    DraftFixture fx;
    SamplePromoter promoter( fx.store );
    const auto &memberA = fx.samples[0].sampleId();
    const auto &memberB = fx.samples[1].sampleId();

    // No event group → refused (pairs without a leakage key are unauditable).
    const auto noEvent =
        promoter.promotePair( fx.versionId, fx.source(), memberA, memberB,
                              QStringLiteral( "pre_post" ), QString() );
    REQUIRE( !noEvent.has_value() );
    CHECK( noEvent.diagnostics().first().code == QStringLiteral( "dataset.promotion_pair_without_event" ) );

    const auto pairId =
        promoter.promotePair( fx.versionId, fx.source(), memberA, memberB,
                              QStringLiteral( "pre_post" ), QStringLiteral( "event-2024-07" ) );
    REQUIRE( pairId.has_value() );

    // Temporal with a known-missing observation.
    QVector<TemporalMember> members;
    TemporalMember first;
    first.memberSampleId = memberA;
    first.timeUtc = QDateTime::fromString( QStringLiteral( "2024-01-10T00:00:00Z" ), Qt::ISODate );
    members.append( first );
    TemporalMember hole;
    hole.timeUtc = QDateTime::fromString( QStringLiteral( "2024-02-10T00:00:00Z" ), Qt::ISODate );
    hole.missing = true;
    members.append( hole );
    TemporalMember third;
    third.memberSampleId = memberB;
    third.timeUtc = QDateTime::fromString( QStringLiteral( "2024-03-10T00:00:00Z" ), Qt::ISODate );
    members.append( third );
    const auto temporalId =
        promoter.promoteTemporal( fx.versionId, fx.source(), members,
                                  QDateTime::fromString( QStringLiteral( "2024-04-01T00:00:00Z" ),
                                                         Qt::ISODate ) );
    REQUIRE( temporalId.has_value() );

    const auto page = fx.store.samplesPage( fx.versionId, 0, 500 );
    REQUIRE( page.has_value() );
    bool sawPair = false;
    bool sawTemporal = false;
    for ( const auto &sample : page.value().second )
    {
        if ( sample.kind() == SampleKind::Pair )
        {
            sawPair = true;
            const auto payload = std::get<PairSample>( sample.payload() );
            CHECK( payload.primaryRef == memberA );
            CHECK( sample.groupId() == QStringLiteral( "event-2024-07" ) );
            CHECK( sample.provenance().value( QStringLiteral( "event_group" ) ).toString() ==
                   QStringLiteral( "event-2024-07" ) );
        }
        if ( sample.kind() == SampleKind::Temporal )
        {
            sawTemporal = true;
            const auto payload = std::get<TemporalSample>( sample.payload() );
            CHECK( payload.observations.size() == 3 );
            CHECK( payload.observations[1].missing );
        }
    }
    CHECK( sawPair );
    CHECK( sawTemporal );
}

// --- M3: run recorder ------------------------------------------------------------

TEST_CASE( "run recorder records truthful terminal states", "[recorder][experiment]" )
{
    DraftFixture fx;
    sicnu::experiment::ExperimentStore experimentStore;
    REQUIRE( experimentStore.open( fx.dir.filePath( QStringLiteral( "exp.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "77777777-8888-4777-8777-777777777777" ) );
    experiment.setName( QStringLiteral( "recorder" ) );
    experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experimentStore.upsertExperiment( experiment ).has_value() );

    // Commit the version so its fingerprint exists (drafts carry none).
    REQUIRE( fx.store.stageVersion( fx.versionId ).has_value() );
    REQUIRE( fx.store.commitVersion( fx.versionId ).has_value() );

    ExperimentRunRecorder recorder( experimentStore );
    recorder.setDatasetStore( &fx.store );

    RunStartRequest request;
    request.experimentId = experiment.experimentId();
    request.algorithmId = QStringLiteral( "rs:classify" );
    request.algorithmVersion = QStringLiteral( "2.0" );
    request.parameters = QJsonObject{ { QStringLiteral( "b" ), 2 }, { QStringLiteral( "a" ), 1 } };
    request.datasetVersionId = fx.versionId.toString();
    request.splitManifestId = QStringLiteral( "88888888-9999-4888-8888-888888888888" );
    request.seed = 5;
    request.executionRef = QStringLiteral( "workflow-run-1" );

    // Version exists → run starts and the fingerprint is auto-stamped.
    auto started = recorder.startRun( request );
    REQUIRE( started.has_value() );
    const auto run = experimentStore.runById( started.value() );
    REQUIRE( run.has_value() );
    CHECK( run->status() == RunStatus::Running );
    CHECK( !run->datasetFingerprint().isEmpty() );
    // Config hash is key-order free: {b:2,a:1} == {a:1,b:2}.
    QJsonObject reordered{ { QStringLiteral( "a" ), 1 }, { QStringLiteral( "b" ), 2 } };
    CHECK( run->configHash() == runConfigHash( reordered ) );

    // Unknown experiment → refusal.
    RunStartRequest ghost = request;
    ghost.experimentId = QStringLiteral( "00000000-0000-4000-8000-000000000000" );
    const auto refused = recorder.startRun( ghost );
    REQUIRE( !refused.has_value() );
    CHECK( refused.diagnostics().first().code == QStringLiteral( "experiment.recorder_missing_experiment" ) );

    // Unknown dataset version (store wired) → refusal.
    RunStartRequest badVersion = request;
    badVersion.datasetVersionId = QStringLiteral( "99999999-9999-4999-8999-999999999999" );
    const auto badDataset = recorder.startRun( badVersion );
    REQUIRE( !badDataset.has_value() );
    CHECK( badDataset.diagnostics().first().code == QStringLiteral( "experiment.recorder_missing_version" ) );

    // Failure records error evidence, never success.
    REQUIRE( recorder.markFailed( started.value(), QStringLiteral( "oom" ),
                                  QStringLiteral( "worker died" ) )
                 .has_value() );
    const auto failed = experimentStore.runById( started.value() );
    REQUIRE( failed.has_value() );
    CHECK( failed->status() == RunStatus::Failed );
    CHECK( failed->metrics().value( QStringLiteral( "error" ) )
               .toObject()
               .value( QStringLiteral( "error_code" ) )
               .toString() == QStringLiteral( "oom" ) );

    // Terminal correction is a NEW run, not a rewrite.
    auto corrected = recorder.markSucceeded( started.value(), {}, QJsonObject{} );
    REQUIRE( !corrected.has_value() );

    // Cancellation path.
    request.executionRef = QStringLiteral( "workflow-run-2" );
    auto second = recorder.startRun( request );
    REQUIRE( second.has_value() );
    REQUIRE( recorder.markCancelled( second.value(), QStringLiteral( "user requested" ) ).has_value() );
    const auto cancelled = experimentStore.runById( second.value() );
    CHECK( cancelled->status() == RunStatus::Cancelled );
    CHECK( cancelled->metrics().value( QStringLiteral( "cancel_reason" ) ).toString() ==
           QStringLiteral( "user requested" ) );

    // Stale-run reconciliation: a live ref keeps its run out of the report;
    // a non-terminal run whose execution died lands in it. No auto-closing.
    request.executionRef = QStringLiteral( "workflow-run-live" );
    auto live = recorder.startRun( request );
    REQUIRE( live.has_value() );
    request.executionRef = QStringLiteral( "workflow-run-dead" );
    auto dead = recorder.startRun( request );
    REQUIRE( dead.has_value() );
    const auto stale = recorder.reconcileStaleRuns( { QStringLiteral( "workflow-run-live" ) } );
    REQUIRE( stale.size() == 1 );
    CHECK( stale.first().runId == dead.value() );
    CHECK( stale.first().executionRef == QStringLiteral( "workflow-run-dead" ) );
}

// --- M4: fold audit ------------------------------------------------------------

namespace
{

/// A fold manifest over 24 grouped samples; class "water" for group g-0..g-1
/// and "forest" for g-2..g-3 so GroupKFold placements create zero-ratio
/// folds for at least one class.
struct FoldFixture
{
    QVector<SplitInput> inputs;
    SplitManifest manifest;

    FoldFixture()
    {
        for ( int i = 0; i < 24; ++i )
        {
            SplitInput input;
            input.sampleId = QStringLiteral( "s-%1" ).arg( i );
            input.groupId = QStringLiteral( "g-%1" ).arg( i % 4 );
            input.classCode = ( i % 4 ) < 2 ? QStringLiteral( "water" ) : QStringLiteral( "forest" );
            inputs.append( input );
        }
        SplitConfig config;
        config.method = SplitMethod::GroupKFold;
        config.seed = 3;
        config.foldCount = 3;
        const auto generated =
            SplitEngine::generate( config, QStringLiteral( "version-under-test" ), inputs );
        REQUIRE( generated.has_value() );
        manifest = generated.value();
    }
};

} // namespace

TEST_CASE( "fold audit covers every fold with balance and replay", "[fold][audit]" )
{
    FoldFixture fx;
    const auto summary = FoldAuditor::auditFolds( fx.manifest, fx.inputs );
    REQUIRE( summary.has_value() );
    CHECK( summary.value().foldCount == 3 );
    REQUIRE( summary.value().folds.size() == 3 );
    CHECK( summary.value().replayMatches ); // regenerate == stored fingerprint

    qint64 totalTest = 0;
    for ( const auto &fold : summary.value().folds )
    {
        totalTest += fold.testCount;
        CHECK( fold.trainCount + fold.testCount == 24 );
        CHECK( fold.report.auditedChecks().contains( QStringLiteral( "exact_duplicate" ) ) );
    }
    CHECK( totalTest == 24 ); // fold partitions, no double counting

    // Zero-ratio: group-based folds make a class vanish from a fold side.
    bool sawZeroRatio = false;
    for ( const auto &fold : summary.value().folds )
        if ( !fold.classesMissingInTest.isEmpty() || !fold.classesMissingInTrain.isEmpty() )
            sawZeroRatio = true;
    CHECK( sawZeroRatio );

    // Changing the seed changes the assignment fingerprint (determinism is
    // per-seed: the same inputs under seed+1 are a DIFFERENT split).
    QVector<SplitInput> sameInputs = fx.inputs;
    SplitConfig other = fx.manifest.config();
    other.seed = other.seed + 1;
    const auto regenerated =
        SplitEngine::generate( other, fx.manifest.datasetVersionId(), sameInputs );
    REQUIRE( regenerated.has_value() );
    CHECK( regenerated.value().fingerprint() != fx.manifest.fingerprint() );
    const auto replay = FoldAuditor::verifyDeterministicReplay( fx.manifest, sameInputs );
    REQUIRE( replay.has_value() );
    CHECK( replay.value() ); // the original manifest replays from its own config

    // Role-based manifests are refused loudly.
    SplitConfig plain;
    plain.method = SplitMethod::Random;
    plain.seed = 1;
    const auto roleManifest =
        SplitEngine::generate( plain, fx.manifest.datasetVersionId(), fx.inputs );
    REQUIRE( roleManifest.has_value() );
    const auto refused = FoldAuditor::auditFolds( roleManifest.value(), fx.inputs );
    REQUIRE( !refused.has_value() );
    CHECK( refused.diagnostics().first().code == QStringLiteral( "dataset.split_not_fold_based" ) );
}

TEST_CASE( "fold audit surfaces planted duplicate leakage with fold evidence",
           "[fold][audit][leakage]" )
{
    FoldFixture fx;
    // Planted cross-fold duplicates: s-0 (group g-0) and s-1 (group g-1)
    // share one content digest. Under GroupKFold their groups land in
    // different folds, so at least one fold materialization has the pair
    // straddling Train/Test.
    QHash<QString, QString> digest;
    for ( auto &input : fx.inputs )
        digest[input.sampleId] = QStringLiteral( "dg-%1" ).arg( input.groupId );
    // s-0 (g-0), s-1 (g-1), s-2 (g-2), s-3 (g-3) share one digest: whatever
    // the group→fold assignment, at least two of the four groups sit in
    // different folds, so some materialization has a planted pair straddling
    // Train/Test — exactly the leakage the audit must catch.
    for ( int i = 0; i < 4; ++i )
        digest[QStringLiteral( "s-%1" ).arg( i )] = QStringLiteral( "planted-shared" );

    QVector<AuditSample> samples;
    for ( const auto &input : fx.inputs )
    {
        AuditSample sample;
        sample.input = input;
        sample.contentDigest = digest.value( input.sampleId );
        samples.append( sample );
    }

    const auto summary =
        FoldAuditor::auditFolds( fx.manifest, fx.inputs, LeakageAuditConfig(), digest );
    REQUIRE( summary.has_value() );
    int foldsWithFinding = 0;
    for ( const auto &fold : summary.value().folds )
    {
        for ( const auto &finding : fold.report.findings() )
            if ( finding.kind == LeakageKind::ExactDuplicate )
            {
                ++foldsWithFinding;
                break;
            }
    }
    CHECK( foldsWithFinding >= 1 );
}

// --- M5: facets & quality cache ------------------------------------------------

TEST_CASE( "facet distributions are bounded and honest about the tail",
           "[facets][scale]" )
{
    DraftFixture fx( 12 );
    // Attach facets: 12 samples across 3 digest buckets + 2 regions.
    for ( const auto &sample : fx.samples )
    {
        QVector<DatasetStore::FacetEntry> entries;
        entries.append( qMakePair( QStringLiteral( "content_digest" ),
                                   sample.provenance().value( QStringLiteral( "content_digest" ) )
                                       .toString() ) );
        entries.append( qMakePair( QStringLiteral( "region" ),
                                   sample.groupId() == QStringLiteral( "group-a" )
                                       ? QStringLiteral( "north" )
                                       : QStringLiteral( "south" ) ) );
        REQUIRE( fx.store.setSampleFacets( fx.versionId, SampleId::fromString( sample.sampleId() ).value(),
                                           entries )
                     .has_value() );
    }

    const auto digests = fx.store.facetDistribution( fx.versionId, QStringLiteral( "content_digest" ) );
    REQUIRE( digests.has_value() );
    CHECK( digests.value().total == 12 );
    qint64 reported = 0;
    for ( const auto &bucket : digests.value().values )
        reported += bucket.second;
    CHECK( reported == digests.value().total );

    const auto cross =
        fx.store.facetCrossCounts( fx.versionId, QStringLiteral( "region" ), QStringLiteral( "content_digest" ) );
    REQUIRE( cross.has_value() );
    qint64 cellTotal = 0;
    for ( const auto &cell : cross.value() )
        cellTotal += cell.second;
    CHECK( cellTotal == 12 );

    // Committed versions refuse facet writes (frozen content).
    REQUIRE( fx.store.stageVersion( fx.versionId ).has_value() );
    REQUIRE( fx.store.commitVersion( fx.versionId ).has_value() );
    QVector<DatasetStore::FacetEntry> late;
    late.append( qMakePair( QStringLiteral( "region" ), QStringLiteral( "east" ) ) );
    const auto frozen = fx.store.setSampleFacets(
        fx.versionId, SampleId::fromString( fx.samples.first().sampleId() ).value(), late );
    REQUIRE( !frozen.has_value() );
    CHECK( frozen.diagnostics().first().code == QStringLiteral( "dataset.not_draft" ) );
}

TEST_CASE( "duplicate summary counts exact digest buckets", "[facets][duplicates]" )
{
    // Distribution over content_digest buckets: bucket sizes >1 are
    // duplicates. dg-0..dg-2 over 12 samples => 3 buckets of 4.
    DraftFixture fx( 12 );
    for ( const auto &sample : fx.samples )
    {
        QVector<DatasetStore::FacetEntry> entries;
        entries.append( qMakePair( QStringLiteral( "content_digest" ),
                                   sample.provenance().value( QStringLiteral( "content_digest" ) )
                                       .toString() ) );
        REQUIRE( fx.store.setSampleFacets( fx.versionId,
                                           SampleId::fromString( sample.sampleId() ).value(),
                                           entries )
                     .has_value() );
    }
    const auto digests =
        fx.store.facetDistribution( fx.versionId, QStringLiteral( "content_digest" ) );
    REQUIRE( digests.has_value() );
    int duplicateBuckets = 0;
    for ( const auto &bucket : digests.value().values )
        if ( bucket.second > 1 )
            ++duplicateBuckets;
    CHECK( duplicateBuckets == 3 );
}

TEST_CASE( "quality cache detects stale content", "[facets][quality][cache]" )
{
    DraftFixture fx;
    const auto stampBefore = fx.store.sampleContentStamp( fx.versionId );

    QJsonObject summary;
    summary.insert( QStringLiteral( "zero_ratio_classes" ), 0 );
    REQUIRE( fx.store
                 .saveQualitySummary( fx.versionId, stampBefore.first, stampBefore.second, summary )
                 .has_value() );

    const auto cached = fx.store.qualitySummary( fx.versionId );
    REQUIRE( cached.has_value() );
    const auto stampNow = fx.store.sampleContentStamp( fx.versionId );
    CHECK( cached->sampleCount == stampNow.first );
    CHECK( cached->maxRoword == stampNow.second );

    // Mutate the draft: the cache stamp no longer matches live content.
    SampleRecord extra;
    extra.setSampleId( SampleId::generate().toString() );
    extra.setDatasetVersionId( fx.versionId.toString() );
    extra.setKind( SampleKind::Point );
    PointSample extraPoint;
    extraPoint.x = 99;
    extraPoint.y = 99;
    extra.payload() = extraPoint;
    REQUIRE( fx.store.addSamples( { extra } ).has_value() );
    const auto stampAfter = fx.store.sampleContentStamp( fx.versionId );
    const auto stale = fx.store.qualitySummary( fx.versionId );
    REQUIRE( stale.has_value() );
    CHECK( stale->sampleCount != stampAfter.first );
}

// --- M6: replay readiness --------------------------------------------------------

TEST_CASE( "replay readiness never overstates", "[replay][readiness]" )
{
    DraftFixture fx;
    sicnu::experiment::ExperimentStore experimentStore;
    REQUIRE( experimentStore.open( fx.dir.filePath( QStringLiteral( "exp2.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "55555555-6666-4777-8666-666666666666" ) );
    experiment.setName( QStringLiteral( "readiness" ) );
    experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experimentStore.upsertExperiment( experiment ).has_value() );
    const QString readinessExperimentId = experiment.experimentId();

    // A stored split manifest so the run can pin it.
    SplitConfig config;
    config.method = SplitMethod::Random;
    config.seed = 11;
    QVector<SplitInput> inputs;
    for ( const auto &sample : fx.samples )
    {
        SplitInput input;
        input.sampleId = sample.sampleId();
        input.groupId = sample.groupId();
        inputs.append( input );
    }
    const auto split =
        SplitEngine::generate( config, fx.versionId.toString(), inputs );
    REQUIRE( split.has_value() );
    REQUIRE( fx.store.saveSplitManifest( split.value() ).has_value() );

    ExperimentRun run;
    run.setRunId( QStringLiteral( "66666666-7777-4888-8777-777777777777" ) );
    run.setExperimentId( readinessExperimentId );
    run.setDatasetVersionId( fx.versionId.toString() );
    run.setSplitManifestId( split.value().manifestId() );
    run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
    run.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );

    // No dataset store: dataset/split checks are unknown (not ok!).
    ReproductionHooks hooks;
    const auto unknownStore = ReplayReadiness::assess( run, nullptr, hooks );
    CHECK( unknownStore.level == sicnu::dataset::ReproductionLevel::BestEffort );

    // Wired store: dataset + split ok, algorithm unknown -> BestEffort.
    const auto wired = ReplayReadiness::assess( run, &fx.store, hooks );
    CHECK( wired.level == sicnu::dataset::ReproductionLevel::BestEffort );

    // A run pinning a MISSING artifact is Impossible via the artifact hook.
    sicnu::experiment::ExperimentRun::Artifact artifact;
    artifact.path = QStringLiteral( "definitely/not/here.tif" );
    artifact.sizeBytes = 10;
    run.artifacts().append( artifact );
    hooks.artifactAvailable = []( const QString &path, qint64 size ) {
        QFileInfo info( path );
        return info.exists() && ( size <= 0 || info.size() == size );
    };
    const auto missing = ReplayReadiness::assess( run, &fx.store, hooks );
    CHECK( missing.level == sicnu::dataset::ReproductionLevel::Impossible );
    CHECK( !missing.missingDependencyDiagnostics().isEmpty() );

    // Equivalent-run lookup: same identity pins => same execution fingerprint.
    ExperimentRun twin = run;
    twin.setRunId( QStringLiteral( "77777777-8888-4999-8777-888888888888" ) );
    twin.setExperimentId( readinessExperimentId );
    twin.artifacts().clear();
    ExperimentRun other = run;
    other.setRunId( QStringLiteral( "88888888-9999-4000-8777-999999999999" ) );
    other.setExperimentId( readinessExperimentId );
    other.setAlgorithmId( QStringLiteral( "rs:other" ) );
    REQUIRE( experimentStore.upsertRun( run ).has_value() );
    REQUIRE( experimentStore.upsertRun( twin ).has_value() );
    REQUIRE( experimentStore.upsertRun( other ).has_value() );
    const QString fingerprint = runExecutionFingerprint( run.executionIdentity() );
    const auto equivalent =
        ReplayReadiness::equivalentRuns( experimentStore, fingerprint, run.runId() );
    CHECK( equivalent.contains( twin.runId() ) );
    CHECK( !equivalent.contains( other.runId() ) );
}

// --- M7: comparison extensions --------------------------------------------------

TEST_CASE( "protocol compatibility detects every differing dimension",
           "[comparison][protocol]" )
{
    EvaluationProtocol a;
    a.setDatasetVersionId( QStringLiteral( "v1" ) );
    a.setSplitManifestId( QStringLiteral( "s1" ) );
    a.setSubset( QStringLiteral( "test" ) );
    a.ignoreLabels() = QStringList{ QStringLiteral( "bg" ) };
    EvaluationProtocol b = a;
    const auto same = compareProtocols( a, b );
    CHECK( same.compatible );

    b.setSubset( QStringLiteral( "validation" ) );
    b.setAggregation( QStringLiteral( "micro" ) );
    b.setIouThreshold( 0.55 );
    const auto different = compareProtocols( a, b );
    CHECK( !different.compatible );
    CHECK( different.differences.size() == 3 );
    bool sawSubset = false;
    bool sawAggregation = false;
    bool sawIou = false;
    for ( const auto &difference : different.differences )
    {
        sawSubset |= difference.startsWith( QStringLiteral( "subset:" ) );
        sawAggregation |= difference.startsWith( QStringLiteral( "aggregation:" ) );
        sawIou |= difference.startsWith( QStringLiteral( "iou_threshold:" ) );
    }
    CHECK( sawSubset );
    CHECK( sawAggregation );
    CHECK( sawIou );
}

TEST_CASE( "schema compatibility verdicts on known cases", "[comparison][schema]" )
{
    const auto base = twoClassSchema();

    // Identical id+version -> compatible.
    const auto same = compareLabelSchemas( base, base );
    CHECK( same.verdict == SchemaCompatibility::Verdict::Compatible );

    // Version bump with an added class -> compatible with differences.
    LabelSchema extended = base;
    extended.setVersion( 2 );
    LabelClass builtup;
    builtup.setStableId( QStringLiteral( "44444444-4444-4444-8444-444444444444" ) );
    builtup.setCode( QStringLiteral( "builtup" ) );
    extended.classes().append( builtup );
    const auto added = compareLabelSchemas( base, extended );
    CHECK( added.verdict == SchemaCompatibility::Verdict::CompatibleWithDifferences );
    CHECK( added.addedCodes == QStringList{ QStringLiteral( "builtup" ) } );

    // Removed class -> not comparable.
    const auto removed = compareLabelSchemas( extended, base );
    CHECK( removed.verdict == SchemaCompatibility::Verdict::NotComparable );

    // Different schema identities -> not comparable.
    LabelSchema foreign = base;
    foreign.setSchemaId( QStringLiteral( "99999999-9999-4999-8999-999999999999" ) );
    const auto foreignCompare = compareLabelSchemas( base, foreign );
    CHECK( foreignCompare.verdict == SchemaCompatibility::Verdict::NotComparable );
}

TEST_CASE( "paired comparison flags insufficient support instead of faking it",
           "[comparison][paired]" )
{
    MetricRecord a;
    a.protocol.setDatasetVersionId( QStringLiteral( "v" ) );
    a.metrics = QJsonObject{
        { QStringLiteral( "overall_accuracy" ), 0.80 },
        { QStringLiteral( "per_class" ),
          QJsonObject{
              { QStringLiteral( "water" ),
                QJsonObject{ { QStringLiteral( "f1" ), 0.7 },
                             { QStringLiteral( "support" ), 50 } } },
              { QStringLiteral( "rare" ),
                QJsonObject{ { QStringLiteral( "f1" ), 0.9 },
                             { QStringLiteral( "support" ), 3 } } } } } };
    MetricRecord b = a;
    b.metrics = QJsonObject{
        { QStringLiteral( "overall_accuracy" ), 0.84 },
        { QStringLiteral( "per_class" ),
          QJsonObject{
              { QStringLiteral( "water" ),
                QJsonObject{ { QStringLiteral( "f1" ), 0.75 },
                             { QStringLiteral( "support" ), 60 } } },
              { QStringLiteral( "rare" ),
                QJsonObject{ { QStringLiteral( "f1" ), 0.95 },
                             { QStringLiteral( "support" ), 4 } } } } } };

    const auto summary = pairedRunComparison( a, b, nullptr, 30 );
    CHECK( summary.protocolsCompatible );
    bool sawWater = false;
    bool sawRare = false;
    for ( const auto &delta : summary.deltas )
    {
        if ( delta.metric == QLatin1String( "overall_accuracy" ) )
            CHECK( std::abs( delta.delta - 0.04 ) < 1e-12 );
        if ( delta.metric.endsWith( QLatin1String( "water::f1" ) ) )
        {
            sawWater = true;
            CHECK( !delta.insufficientSupport );
            CHECK( delta.supportA == 50 );
        }
        if ( delta.metric.endsWith( QLatin1String( "rare::f1" ) ) )
        {
            sawRare = true;
            CHECK( delta.insufficientSupport ); // support 3/4 < 30 — flagged
        }
    }
    CHECK( sawWater );
    CHECK( sawRare );
}
