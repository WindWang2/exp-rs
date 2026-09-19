// test_d19_foundry_benchmark_chain.cpp — hermetic headless E2E for the D19
// Dataset Foundry → Benchmark → Experiment pin chain (no Qt GUI).
#include <catch2/catch_test_macros.hpp>

#include "agent/data_platform_tools.h"
#include "dataset/annotation.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/feature_table.h"
#include "dataset/foundry_service.h"
#include "dataset/sample.h"
#include "dataset/sample_catalog.h"
#include "dataset/split.h"
#include "experiment/benchmark_definition.h"
#include "experiment/benchmark_runner.h"
#include "experiment/benchmark_service.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

#include <QDateTime>
#include <QHash>
#include <QTemporaryDir>
#include <QVariantMap>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

struct ChainFixture
{
    QTemporaryDir dir;
    DatasetStore datasetStore;
    ExperimentStore experimentStore;
    QString datasetId;
    QString versionId;
    QString splitId;
    QStringList sampleIds;

    ChainFixture()
    {
        REQUIRE( dir.isValid() );
        REQUIRE( datasetStore.open( dir.filePath( QStringLiteral( "dataset.sqlite" ) ) ) );
        REQUIRE( experimentStore.open( dir.filePath( QStringLiteral( "experiment.sqlite" ) ) ) );

        datasetId = DatasetId::generate().toString();
        versionId = DatasetVersionId::generate().toString();
        REQUIRE( datasetStore
                     .createDataset( DatasetId::fromString( datasetId ).value(),
                                     QStringLiteral( "D19 chain" ) )
                     .has_value() );

        DatasetManifest manifest;
        manifest.setDatasetId( datasetId );
        manifest.setVersionId( versionId );
        manifest.setName( QStringLiteral( "D19 chain fixture" ) );
        manifest.setRole( DatasetRole::Benchmark );
        manifest.setCreatedAtUtc( QDateTime::fromString(
            QStringLiteral( "2026-09-15T00:00:00.000Z" ), Qt::ISODateWithMs ) );
        SourceAssetRef source;
        source.assetId = QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" );
        source.revision = 1;
        source.role = QStringLiteral( "image" );
        manifest.sourceAssets().append( source );
        DatasetEntry entry;
        entry.kind = QStringLiteral( "asset" );
        entry.refId = source.assetId;
        entry.role = QStringLiteral( "image" );
        manifest.entries().append( entry );
        manifest.labelSchema().schemaId = QStringLiteral( "lc" );
        manifest.labelSchema().version = 1;
        // The fixture declares a schema CRS while its point samples carry no
        // per-sample CRS: the dataset:qa façade must then report the crs
        // category as unknown (#1037 F-1030-P2-crs-pass), never pass.
        manifest.schema().crs = QStringLiteral( "EPSG:32650" );

        REQUIRE( datasetStore.createDraftVersion( manifest ).has_value() );

        QVector<SampleRecord> samples;
        QVector<SplitInput> splitInputs;
        for ( int i = 0; i < 8; ++i )
        {
            SampleRecord sample;
            const QString sid = SampleId::generate().toString();
            sampleIds.append( sid );
            sample.setSampleId( sid );
            sample.setDatasetVersionId( versionId );
            sample.setKind( SampleKind::Point );
            sample.setGroupId( i < 4 ? QStringLiteral( "region-a" ) : QStringLiteral( "region-b" ) );
            PointSample point;
            point.x = i;
            point.y = 0;
            sample.payload() = point;
            sample.provenance().insert( QStringLiteral( "sensor" ), QStringLiteral( "S2" ) );
            sample.provenance().insert( QStringLiteral( "region" ),
                                        i < 4 ? QStringLiteral( "A" ) : QStringLiteral( "B" ) );
            sample.provenance().insert( QStringLiteral( "year" ), 2025 );
            sample.provenance().insert( QStringLiteral( "modality" ), QStringLiteral( "optical" ) );
            samples.append( sample );

            SplitInput input;
            input.sampleId = sid;
            input.groupId = sample.groupId();
            splitInputs.append( input );
        }
        REQUIRE( datasetStore.addSamples( samples ).has_value() );

        for ( int i = 0; i < sampleIds.size(); ++i )
        {
            AnnotationRecord ann;
            ann.setAnnotationId( AnnotationId::generate().toString() );
            ann.setTargetSampleId( sampleIds[i] );
            ann.setDatasetVersionId( versionId );
            ann.setRevision( 1 );
            ann.setLabelSchemaId( QStringLiteral( "lc" ) );
            ann.setLabelSchemaVersion( 1 );
            ann.setClassCode( i % 2 == 0 ? QStringLiteral( "water" ) : QStringLiteral( "land" ) );
            ann.setSourceType( i == 0 ? AnnotationSourceType::Pseudo : AnnotationSourceType::Human );
            if ( i == 0 )
            {
                ann.sourceDetail().insert( QStringLiteral( "model_id" ),
                                           QStringLiteral( "pseudo-model" ) );
                ann.sourceDetail().insert( QStringLiteral( "model_digest" ), QStringLiteral( "pd" ) );
                ann.sourceDetail().insert( QStringLiteral( "threshold" ), 0.5 );
            }
            ann.setCreatedAtUtc( QDateTime::fromString(
                QStringLiteral( "2026-09-15T01:00:00.000Z" ), Qt::ISODateWithMs ) );
            REQUIRE( datasetStore.addAnnotation( ann ).has_value() );
        }

        SplitConfig config;
        config.method = SplitMethod::Random;
        config.seed = 7;
        config.trainRatio = 0.5;
        config.validationRatio = 0.25;
        config.testRatio = 0.25;
        const auto generated = SplitEngine::generate( config, versionId, splitInputs );
        REQUIRE( generated.has_value() );
        splitId = generated->manifestId();
        REQUIRE( datasetStore.saveSplitManifest( generated.value() ).has_value() );

        REQUIRE( datasetStore.stageVersion( DatasetVersionId::fromString( versionId ).value() )
                     .has_value() );
        REQUIRE( datasetStore.commitVersion( DatasetVersionId::fromString( versionId ).value() )
                     .has_value() );
    }

    QHash<QString, SplitRole> rolesBySample() const
    {
        QHash<QString, SplitRole> map;
        const auto manifest = datasetStore.splitManifestById( splitId );
        REQUIRE( manifest.has_value() );
        for ( const SplitAssignment &assignment : manifest->assignments() )
            map.insert( assignment.sampleId, assignment.role );
        return map;
    }
};

BenchmarkDefinition makeBench( const ChainFixture &fx )
{
    BenchmarkDefinition def;
    def.setBenchmarkId( QStringLiteral( "chain-class-v1" ) );
    def.setBenchmarkVersion( 1 );
    def.setName( QStringLiteral( "Chain classification" ) );
    def.setTaskFamily( BenchmarkTaskFamily::Classification );
    def.setDatasetVersionId( fx.versionId );
    def.setSplitManifestId( fx.splitId );
    def.setLabelSchemaId( QStringLiteral( "lc" ) );
    def.setLabelSchemaVersion( 1 );
    def.metricNames() = QStringList{ QStringLiteral( "overall_accuracy" ),
                                     QStringLiteral( "kappa" ), QStringLiteral( "macro_f1" ) };
    def.protocol().setDatasetVersionId( fx.versionId );
    def.protocol().setSplitManifestId( fx.splitId );
    def.protocol().setSubset( QStringLiteral( "test" ) );
    def.setRefusePseudoLabelsInTest( true );
    def.setSeedPolicy( 7 );
    return def;
}

} // namespace

TEST_CASE( "D19 hermetic foundry→benchmark→experiment chain",
           "[d19][e2e][chain][hermetic]" )
{
    ChainFixture fx;
    DatasetFoundryService foundry( &fx.datasetStore );
    const auto roles = fx.rolesBySample();

    const auto inspect =
        foundry.inspectVersion( DatasetVersionId::fromString( fx.versionId ).value() );
    REQUIRE( inspect.has_value() );
    CHECK( inspect->value( QStringLiteral( "role" ) ).toString() == QStringLiteral( "benchmark" ) );
    CHECK( inspect->value( QStringLiteral( "sample_count" ) ).toInteger() == 8 );

    QVector<SampleCatalogRow> rows;
    for ( int i = 0; i < fx.sampleIds.size(); ++i )
    {
        SampleCatalogRow row;
        row.sampleId = fx.sampleIds[i];
        row.classCode = i % 2 == 0 ? QStringLiteral( "water" ) : QStringLiteral( "land" );
        row.sensor = QStringLiteral( "S2" );
        row.region = i < 4 ? QStringLiteral( "A" ) : QStringLiteral( "B" );
        row.year = 2025;
        row.splitRole = splitRoleToString( roles.value( fx.sampleIds[i], SplitRole::Unassigned ) );
        row.hasPseudoLabel = ( i == 0 );
        row.labelSource =
            i == 0 ? AnnotationSourceType::Pseudo : AnnotationSourceType::Human;
        rows.append( row );
    }
    SampleCatalogFilter testOnly;
    testOnly.splitRoles = QStringList{ QStringLiteral( "test" ) };
    const SampleCatalogPage testPage = foundry.querySamples( rows, testOnly, 0, 50 );
    CHECK( testPage.totalMatched >= 1 );
    const SampleCatalogSummary summary = foundry.summarizeSamples( rows );
    CHECK( summary.total == 8 );
    CHECK( summary.pseudoLabelCount == 1 );

    DatasetQaInputs qaInputs;
    qaInputs.datasetVersionId = fx.versionId;
    qaInputs.splitManifestId = fx.splitId;
    qaInputs.versionFrozen = true;
    qaInputs.provenanceComplete = true;
    qaInputs.catalogSummary = summary;
    const DatasetQaReport qa = foundry.runQa( qaInputs );
    CHECK( qa.overallVerdict() != AuditVerdict::Unknown );

    FeatureSet set;
    set.setFeatureSetId( QStringLiteral( "fs-chain" ) );
    set.setSchemaVersion( 1 );
    set.setSampleKey( QStringLiteral( "sample_id" ) );
    set.setInputDatasetVersionId( fx.versionId );
    set.setProducer( QStringLiteral( "test" ) );
    FeatureColumn col;
    col.name = QStringLiteral( "ndvi" );
    col.dtype = QStringLiteral( "float64" );
    set.columns().append( col );
    REQUIRE( set.validate().has_value() );
    QVector<FeatureRow> featureRows;
    for ( const QString &sid : fx.sampleIds )
    {
        FeatureRow fr;
        fr.sampleId = sid;
        fr.values.insert( QStringLiteral( "ndvi" ), 0.4 );
        featureRows.append( fr );
    }
    const FeatureJoinResult joined =
        foundry.joinFeatures( set, featureRows, fx.sampleIds, fx.versionId );
    CHECK( joined.verdict == AuditVerdict::Pass );
    CHECK( joined.joined.size() == fx.sampleIds.size() );

    BenchmarkService service( &fx.experimentStore );
    const BenchmarkDefinition def = makeBench( fx );
    REQUIRE( service.publishDefinition( def ).has_value() );

    // Evaluate on human-labeled test samples only.
    QStringList testSampleIds;
    for ( auto it = roles.constBegin(); it != roles.constEnd(); ++it )
    {
        if ( it.value() == SplitRole::Test && it.key() != fx.sampleIds[0] )
            testSampleIds.append( it.key() );
    }
    REQUIRE( !testSampleIds.isEmpty() );

    BenchmarkRunRequest request;
    request.definition = def;
    request.modelId = QStringLiteral( "model-chain" );
    request.modelDigest = QStringLiteral( "mdigest" );
    request.softwareRevision = QStringLiteral( "rev-d19" );
    request.seed = 7;
    request.store = &fx.datasetStore;
    for ( const QString &sid : testSampleIds )
    {
        const int idx = fx.sampleIds.indexOf( sid );
        BenchmarkTruth truth;
        truth.sampleId = sid;
        truth.truthClass = idx % 2 == 0 ? QStringLiteral( "water" ) : QStringLiteral( "land" );
        truth.labelSource = AnnotationSourceType::Human;
        request.truths.append( truth );
        BenchmarkPrediction pred;
        pred.sampleId = sid;
        pred.predictedClass = truth.truthClass;
        request.predictions.append( pred );
    }
    const auto resultA = service.run( request );
    REQUIRE( resultA.has_value() );
    CHECK( resultA->status() == BenchmarkRunStatus::Completed );
    CHECK( resultA->reproducibilityComplete() );

    request.predictions.last().predictedClass =
        request.predictions.last().predictedClass == QLatin1String( "water" )
            ? QStringLiteral( "land" )
            : QStringLiteral( "water" );
    const auto resultB = service.run( request );
    REQUIRE( resultB.has_value() );
    const BenchmarkComparison comparison =
        service.compare( resultA->resultId(), resultB->resultId() );
    CHECK( comparison.comparable );
    CHECK( !comparison.deltas.isEmpty() );

    Experiment exp;
    exp.setExperimentId( ExperimentId::generate().toString() );
    exp.setName( QStringLiteral( "chain" ) );
    REQUIRE( fx.experimentStore.upsertExperiment( exp ).has_value() );

    ExperimentRun run;
    run.setRunId( RunId::generate().toString() );
    run.setExperimentId( exp.experimentId() );
    run.setAlgorithmId( QStringLiteral( "chain-clf" ) );
    run.setDatasetVersionId( fx.versionId );
    run.setSplitManifestId( fx.splitId );
    run.setBenchmarkDefinitionId( def.benchmarkId() );
    run.setBenchmarkDefinitionVersion( def.benchmarkVersion() );
    run.setModelId( QStringLiteral( "model-chain" ) );
    run.setModelDigest( QStringLiteral( "mdigest" ) );
    run.setSeed( 7 );
    run.setCreatedAtUtc( QDateTime::fromString( QStringLiteral( "2026-09-15T02:00:00.000Z" ),
                                                Qt::ISODateWithMs ) );
    REQUIRE( fx.experimentStore.upsertRun( run ).has_value() );
    const auto loaded = fx.experimentStore.runById( run.runId() );
    REQUIRE( loaded.has_value() );
    CHECK( loaded->benchmarkDefinitionId() == def.benchmarkId() );

    QVariantMap qaArgs;
    qaArgs.insert( QStringLiteral( "dataset_db" ),
                   fx.dir.filePath( QStringLiteral( "dataset.sqlite" ) ) );
    qaArgs.insert( QStringLiteral( "version" ), fx.versionId );
    qaArgs.insert( QStringLiteral( "split_manifest_id" ), fx.splitId );
    auto qaTool = sicnu::agent::handleDataPlatformTool( QStringLiteral( "dataset:qa" ), qaArgs );
    CHECK( qaTool.contains( QStringLiteral( "overall" ) ) );
    CHECK( qaTool.contains( QStringLiteral( "scanned" ) ) );
    CHECK( qaTool.contains( QStringLiteral( "scan_capped" ) ) );
    CHECK( qaTool.contains( QStringLiteral( "sample_count" ) ) );
    CHECK( qaTool.value( QStringLiteral( "sample_count" ) ).toLongLong() >= 1 );
    // Labels must not claim Pass when the agent façade did not run label QA.
    bool sawLabelsUnknown = false;
    bool sawCrsUnknown = false;
    const QVariantList cats = qaTool.value( QStringLiteral( "categories" ) ).toList();
    for ( const QVariant &c : cats )
    {
        const QVariantMap cat = c.toMap();
        if ( cat.value( QStringLiteral( "name" ) ).toString() == QLatin1String( "labels" ) )
        {
            sawLabelsUnknown =
                cat.value( QStringLiteral( "verdict" ) ).toString() == QLatin1String( "unknown" );
        }
        // Façade-level CRS honesty (#1037 F-1030-P2-crs-pass): the fixture
        // declares EPSG:32650 but no scanned sample carries a CRS — the crs
        // category must say unknown, never pass.
        if ( cat.value( QStringLiteral( "name" ) ).toString() == QLatin1String( "crs" ) )
        {
            sawCrsUnknown =
                cat.value( QStringLiteral( "verdict" ) ).toString() == QLatin1String( "unknown" );
        }
    }
    CHECK( sawLabelsUnknown );
    CHECK( sawCrsUnknown );

    QVariantMap sampleArgs = qaArgs;
    sampleArgs.insert( QStringLiteral( "limit" ), 10 );
    auto sampleTool =
        sicnu::agent::handleDataPlatformTool( QStringLiteral( "dataset:sample_query" ), sampleArgs );
    CHECK( sampleTool.value( QStringLiteral( "matched" ) ).toLongLong() >= 1 );

    // GOAL naming aliases
    QVariantMap versionArgs;
    versionArgs.insert( QStringLiteral( "dataset_db" ),
                        fx.dir.filePath( QStringLiteral( "dataset.sqlite" ) ) );
    versionArgs.insert( QStringLiteral( "dataset" ), fx.datasetId );
    auto versionsTool =
        sicnu::agent::handleDataPlatformTool( QStringLiteral( "dataset:versions" ), versionArgs );
    CHECK( versionsTool.value( QStringLiteral( "total" ) ).toLongLong() >= 1 );

    QVariantMap benchArgs;
    benchArgs.insert( QStringLiteral( "experiment_db" ),
                      fx.dir.filePath( QStringLiteral( "experiment.sqlite" ) ) );
    auto listed =
        sicnu::agent::handleDataPlatformTool( QStringLiteral( "benchmark:list" ), benchArgs );
    CHECK( listed.value( QStringLiteral( "total" ) ).toLongLong() >= 1 );

    QVariantMap inspectArgs = benchArgs;
    inspectArgs.insert( QStringLiteral( "benchmark" ), def.benchmarkId() );
    inspectArgs.insert( QStringLiteral( "benchmark_version" ), 1 );
    auto inspected =
        sicnu::agent::handleDataPlatformTool( QStringLiteral( "benchmark:inspect" ), inspectArgs );
    CHECK( inspected.value( QStringLiteral( "benchmark_id" ) ).toString() == def.benchmarkId() );

    QVariantMap compareArgs = benchArgs;
    compareArgs.insert( QStringLiteral( "a" ), resultA->resultId() );
    compareArgs.insert( QStringLiteral( "b" ), resultB->resultId() );
    auto compared =
        sicnu::agent::handleDataPlatformTool( QStringLiteral( "benchmark:compare" ), compareArgs );
    CHECK( compared.value( QStringLiteral( "comparable" ) ).toBool() );
}

TEST_CASE( "D19 chain refuses pseudo labels on protected test via runner",
           "[d19][e2e][pseudo][hermetic]" )
{
    ChainFixture fx;
    BenchmarkService service( &fx.experimentStore );
    REQUIRE( service.publishDefinition( makeBench( fx ) ).has_value() );

    BenchmarkRunRequest request;
    request.definition = makeBench( fx );
    request.modelDigest = QStringLiteral( "md" );
    request.softwareRevision = QStringLiteral( "rev" );
    BenchmarkTruth truth;
    truth.sampleId = fx.sampleIds[1];
    truth.truthClass = QStringLiteral( "land" );
    truth.labelSource = AnnotationSourceType::Pseudo;
    request.truths.append( truth );
    BenchmarkPrediction pred;
    pred.sampleId = fx.sampleIds[1];
    pred.predictedClass = QStringLiteral( "land" );
    request.predictions.append( pred );

    const auto result = service.run( request );
    REQUIRE( result.has_value() );
    CHECK( result->status() == BenchmarkRunStatus::Failed );
    CHECK( result->failureCode() == QStringLiteral( "experiment.benchmark_pseudo_in_test" ) );
}

TEST_CASE( "D19 hermetic LeaveOne*/Temporal configs pin as benchmark modes",
           "[d19][e2e][split][leaveone][hermetic]" )
{
    // Scientific modes are configuration over existing SplitMethod — not a
    // second split engine (DECISIONS D5 / SPLIT_MODEL).
    auto makeInputs = []( int n ) {
        QVector<SplitInput> inputs;
        for ( int i = 0; i < n; ++i )
        {
            SplitInput input;
            input.sampleId = QStringLiteral( "x-%1" ).arg( i );
            input.groupId = ( i < n / 2 ) ? QStringLiteral( "region-a" )
                                          : QStringLiteral( "region-b" );
            input.classCode =
                ( i % 2 == 0 ) ? QStringLiteral( "water" ) : QStringLiteral( "land" );
            input.year = 2023 + ( i % 3 );
            input.timeMs = 1700000000000LL + ( i * 86400000LL );
            input.sceneId = QStringLiteral( "scene-%1" ).arg( i % 4 );
            inputs.append( input );
        }
        return inputs;
    };
    const QVector<SplitInput> inputs = makeInputs( 24 );
    const QString versionId = QStringLiteral( "11111111-1111-4111-8111-111111111111" );

    SplitConfig loro;
    loro.method = SplitMethod::LeaveOneRegionOut;
    loro.seed = 11;
    loro.regionKey = QStringLiteral( "region" );
    const auto loroManifest = SplitEngine::generate( loro, versionId, inputs );
    REQUIRE( loroManifest.has_value() );
    CHECK( SplitEngine::methodUsesFolds( loroManifest->config().method ) );
    const auto fold0 = loroManifest->materializeFold( 0 );
    REQUIRE( fold0.has_value() );
    CHECK( !fold0->isEmpty() );

    SplitConfig loyo;
    loyo.method = SplitMethod::LeaveOneYearOut;
    loyo.seed = 11;
    const auto loyoManifest = SplitEngine::generate( loyo, versionId, inputs );
    REQUIRE( loyoManifest.has_value() );
    CHECK( loyoManifest->config().method == SplitMethod::LeaveOneYearOut );

    SplitConfig temporal;
    temporal.method = SplitMethod::Temporal;
    temporal.seed = 11;
    temporal.trainRatio = 0.5;
    temporal.validationRatio = 0.25;
    temporal.testRatio = 0.25;
    const auto temporalManifest = SplitEngine::generate( temporal, versionId, inputs );
    REQUIRE( temporalManifest.has_value() );
    CHECK( temporalManifest->config().method == SplitMethod::Temporal );

    auto pinMode = [&]( const SplitManifest &split, const QString &modeName,
                        const QString &benchId ) {
        BenchmarkDefinition def;
        def.setBenchmarkId( benchId );
        def.setBenchmarkVersion( 1 );
        def.setName( modeName );
        def.setTaskFamily( BenchmarkTaskFamily::Classification );
        def.setDatasetVersionId( versionId );
        def.setSplitManifestId( split.manifestId() );
        def.setLabelSchemaId( QStringLiteral( "lc" ) );
        def.setLabelSchemaVersion( 1 );
        def.metricNames() = QStringList{ QStringLiteral( "overall_accuracy" ) };
        def.protocol().setDatasetVersionId( versionId );
        def.protocol().setSplitManifestId( split.manifestId() );
        def.protocol().setSubset(
            SplitEngine::methodUsesFolds( split.config().method )
                ? QStringLiteral( "fold:0" )
                : QStringLiteral( "test" ) );
        def.setRefusePseudoLabelsInTest( true );
        def.forbiddenLeakage() =
            QStringList{ leakageKindToString( LeakageKind::ExactDuplicate ),
                         leakageKindToString( LeakageKind::TemporalFutureLeakage ) };
        def.metadata().insert( QStringLiteral( "benchmark_mode" ), modeName );
        def.metadata().insert( QStringLiteral( "split_method" ),
                               splitMethodToString( split.config().method ) );
        REQUIRE( def.validate().has_value() );
        const auto roundTrip = BenchmarkDefinition::fromJson( def.toJson() );
        REQUIRE( roundTrip.has_value() );
        CHECK( roundTrip->metadata().value( QStringLiteral( "split_method" ) ).toString() ==
               splitMethodToString( split.config().method ) );
        CHECK( roundTrip->forbiddenLeakage().contains(
            leakageKindToString( LeakageKind::ExactDuplicate ) ) );
        return def.contentDigest();
    };

    const QString d1 =
        pinMode( *loroManifest, QStringLiteral( "cross-region" ),
                 QStringLiteral( "bench-loro" ) );
    const QString d2 =
        pinMode( *loyoManifest, QStringLiteral( "cross-year" ),
                 QStringLiteral( "bench-loyo" ) );
    const QString d3 =
        pinMode( *temporalManifest, QStringLiteral( "temporal-holdout" ),
                 QStringLiteral( "bench-temporal" ) );
    CHECK( d1 != d2 );
    CHECK( d2 != d3 );
}
