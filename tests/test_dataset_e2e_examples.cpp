// test_dataset_e2e_examples.cpp — Foundation 5.0 end-to-end examples
// (goal §54): four small, fully in-memory scenarios exercising the whole
// chain asset-ref → dataset version → samples → split → leakage →
// experiment run → metrics → reproduction bundle. No large data; no raster
// IO (patch specs reference window geometries only).
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_manifest.h"
#include "dataset/annotation.h"
#include "dataset/dataset_quality.h"
#include "dataset/dataset_store.h"
#include "dataset/label_schema.h"
#include "dataset/leakage_audit.h"
#include "dataset/sample.h"
#include "dataset/split.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/reproduction_bundle.h"

#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

struct ScenarioStores
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;

    QString datasetId;
    QString versionId;  // committed
    QString versionFingerprint;
    QString schemaId;

    void open()
    {
        REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
        REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    }

    /// Creates a dataset + committed version with the given source asset.
    void commitVersion( const QString &assetId, const QString &name, const QString &modality,
                        const QString &crs )
    {
        const DatasetId id = DatasetId::generate();
        REQUIRE( datasets.createDataset( id, name ).has_value() );
        datasetId = id.toString();
        DatasetManifest manifest;
        manifest.setDatasetId( datasetId );
        manifest.setVersionId( DatasetVersionId::generate().toString() );
        manifest.setName( name );
        SourceAssetRef source;
        source.assetId = assetId;
        source.revision = 1;
        source.role = QStringLiteral( "image" );
        manifest.sourceAssets().append( source );
        manifest.schema().modality = modality;
        manifest.schema().crs = crs;
        manifest.schema().resolutionX = 10.0;
        manifest.schema().resolutionY = 10.0;
        const auto draft = datasets.createDraftVersion( manifest );
        REQUIRE( draft.has_value() );
        versionId = draft->versionId();
        // Draft stays open: samples land in the next steps; ensureCommitted()
        // freezes the version once the scenario is done building it.
    }

    void ensureCommitted()
    {
        if ( !versionFingerprint.isEmpty() )
            return;
        REQUIRE( datasets.stageVersion(
                     DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) )
                     .has_value() );
        const auto committed = datasets.commitVersion(
            DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) );
        REQUIRE( committed.has_value() );
        versionFingerprint = committed->fingerprint();
    }

    void addPatchSamples( int count, int scenes, const QString &labelPrefix )
    {
        QVector<SampleRecord> batch;
        for ( int i = 0; i < count; ++i )
        {
            SampleRecord sample;
            sample.setSampleId( SampleId::generate().toString() );
            sample.setDatasetVersionId( versionId );
            sample.setKind( SampleKind::Patch );
            sample.setGroupId( QStringLiteral( "scene-%1" ).arg( i % scenes ) );
            PatchSample payload;
            payload.window = PixelWindow{ ( i % scenes ) * 300LL, qint64( i * 10 ), 256, 256 };
            payload.borderPolicy = BorderPolicy::Drop;
            payload.noDataMode = NoDataMode::KeepWithFlag;
            payload.generatorConfigHash = QStringLiteral( "e2e" );
            sample.payload() = payload;
            batch.append( sample );
            if ( batch.size() == 500 )
            {
                REQUIRE( datasets.addSamples( batch ).has_value() );
                batch.clear();
            }
        }
        if ( !batch.isEmpty() )
            REQUIRE( datasets.addSamples( batch ).has_value() );
        (void)labelPrefix;
    }

    /// Runs the full scientific loop: split → audit → run → metrics → bundle.
    /// Returns the comparison verdict inputs callers may reuse.
    QString runExperiment( const QString &algorithmId, const QJsonObject &metrics,
                           const QVector<SplitInput> &inputs, const SplitConfig &config )
    {
        // Freeze the dataset version before binding runs to it.
        ensureCommitted();

        // Split + leakage audit.
        const auto split = SplitEngine::generate( config, versionId, inputs );
        REQUIRE( split.has_value() );
        QVector<AuditSample> auditSamples;
        for ( const SplitInput &input : inputs )
        {
            AuditSample sample;
            sample.input = input;
            const auto assignment = split->assignmentOf( input.sampleId );
            sample.role = assignment ? assignment->role : SplitRole::Unassigned;
            auditSamples.append( sample );
        }
        LeakageAuditConfig auditConfig;
        const auto report = LeakageAuditor::audit( versionId, split->manifestId(),
                                                   auditSamples, auditConfig );
        REQUIRE( report.has_value() );
        CHECK( !report->auditedChecks().isEmpty() );

        // Experiment + run bound to dataset + split pins.
        Experiment experiment;
        experiment.setExperimentId( ExperimentId::generate().toString() );
        experiment.setName( algorithmId );
        experiment.setObjective( QStringLiteral( "e2e example" ) );
        experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
        REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

        ExperimentRun run;
        run.setRunId( RunId::generate().toString() );
        run.setExperimentId( experiment.experimentId() );
        run.setAlgorithmId( algorithmId );
        run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
        run.setParameters( QJsonObject{ { QStringLiteral( "e2e" ), true } } );
        run.setDatasetVersionId( versionId );
        run.setDatasetFingerprint( versionFingerprint );
        run.setSplitManifestId( split->manifestId() );
        run.setSplitFingerprint( split->fingerprint() );
        run.setSeed( config.seed );
        run.setEnvironment( RunEnvironment::fromFields(
            QJsonObject{ { QStringLiteral( "platform" ), QStringLiteral( "test" ) } } ) );
        run.setSoftwareRevision( QStringLiteral( "test" ) );
        run.setStatus( RunStatus::Created );
        REQUIRE( experiments.upsertRun( run ).has_value() );
        run.setStatus( RunStatus::Running );
        REQUIRE( experiments.upsertRun( run ).has_value() );
        run.setMetrics( metrics );
        run.setStatus( RunStatus::Completed );
        run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
        REQUIRE( experiments.upsertRun( run ).has_value() );

        // Protocol-bound metric record.
        MetricRecord record;
        record.runId = run.runId();
        record.protocol.setDatasetVersionId( versionId );
        record.protocol.setSplitManifestId( split->manifestId() );
        record.protocol.setSubset( QStringLiteral( "test" ) );
        record.metrics = metrics;
        REQUIRE( experiments.saveMetricRecord( record ).has_value() );
        return run.runId();
    }
};

QVector<SplitInput> inputsFromStore( ScenarioStores &scenario )
{
    QVector<SplitInput> inputs;
    qint64 offset = 0;
    while ( true )
    {
        const auto page = scenario.datasets.samplesPage(
            DatasetVersionId::fromString( scenario.versionId ).value_or( DatasetVersionId{} ),
            offset, 500 );
        REQUIRE( page.has_value() );
        if ( page->second.isEmpty() )
            break;
        for ( const SampleRecord &sample : page->second )
        {
            SplitInput input;
            input.sampleId = sample.sampleId();
            input.groupId = sample.groupId();
            input.classCode = QStringLiteral( "water" );
            input.validBounds = true;
            const auto &patch = std::get<PatchSample>( sample.payload() );
            input.minX = double( patch.window.x );
            input.minY = double( patch.window.y );
            input.maxX = input.minX + 256.0;
            input.maxY = input.minY + 256.0;
            inputs.append( input );
        }
        offset += page->second.size();
    }
    return inputs;
}

} // namespace

TEST_CASE( "Example A — optical classification chain", "[e2e][optical]" )
{
    ScenarioStores scenario;
    scenario.open();
    scenario.commitVersion( QUuid::createUuid().toString( QUuid::WithoutBraces ),
                            QStringLiteral( "optical-landcover" ), QStringLiteral( "optical" ),
                            QStringLiteral( "EPSG:32650" ) );
    scenario.addPatchSamples( 60, 6, QStringLiteral( "lc" ) );

    // Label schema with a stable ontology + explicit legacy mapping.
    LabelSchema schema;
    schema.setSchemaId( LabelSchemaId::generate().toString() );
    schema.setVersion( 1 );
    LabelClass water;
    water.setStableId( LabelSchemaId::generate().toString() );
    water.setCode( QStringLiteral( "water" ) );
    water.setLegacyIntId( 0 );
    LabelClass urban;
    urban.setStableId( LabelSchemaId::generate().toString() );
    urban.setCode( QStringLiteral( "urban" ) );
    urban.setLegacyIntId( 1 );
    schema.classes() = { water, urban };
    REQUIRE( schema.validate().has_value() );

    SplitConfig config;
    config.method = SplitMethod::Grouped;
    config.seed = 7;
    const QString runId =
        scenario.runExperiment( QStringLiteral( "rs:classify" ),
                                QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.87 } },
                                inputsFromStore( scenario ), config );

    // Reproduction bundle closes the chain.
    ReproductionBundleExporter exporter( scenario.experiments, scenario.datasets );
    ReproductionBundleOptions options;
    options.outputDir = scenario.dir.filePath( QStringLiteral( "bundle-a" ) );
    const auto report = exporter.exportRun( runId, options );
    if ( !report.ok )
    {
        for ( const QString &warning : report.warnings )
            WARN( warning.toStdString() );
    }
    REQUIRE( report.ok );
    const auto validation = exporter.validateBundle( report.bundlePath, {} );
    CHECK( validation.level != ReproductionLevel::Impossible );
}

TEST_CASE( "Example B — SAR pair change detection chain", "[e2e][sar]" )
{
    ScenarioStores scenario;
    scenario.open();
    scenario.commitVersion( QUuid::createUuid().toString( QUuid::WithoutBraces ), QStringLiteral( "sar-pairs" ),
                            QStringLiteral( "sar" ), QStringLiteral( "EPSG:32650" ) );
    scenario.addPatchSamples( 40, 4, QStringLiteral( "pair" ) );

    // Pair semantics: pre/post members share an event group (leakage-visible).
    QVector<SplitInput> inputs = inputsFromStore( scenario );
    for ( int i = 0; i + 1 < inputs.size(); i += 2 )
    {
        inputs[i].eventGroup = QStringLiteral( "event-%1" ).arg( i / 2 );
        inputs[i + 1].eventGroup = inputs[i].eventGroup;
    }

    SplitConfig config;
    config.method = SplitMethod::Random;
    config.seed = 11;
    const auto split = SplitEngine::generate( config, scenario.versionId, inputs );
    REQUIRE( split.has_value() );

    // Pre/post pairs landing in different roles are findings — by design.
    QVector<AuditSample> auditSamples;
    for ( int i = 0; i < inputs.size(); ++i )
    {
        AuditSample sample;
        sample.input = inputs[i];
        sample.pairCounterpartId =
            i % 2 == 0 ? inputs[i + 1].sampleId : inputs[i - 1].sampleId;
        const auto assignment = split->assignmentOf( inputs[i].sampleId );
        sample.role = assignment ? assignment->role : SplitRole::Unassigned;
        auditSamples.append( sample );
    }
    LeakageAuditConfig auditConfig;
    const auto report =
        LeakageAuditor::audit( scenario.versionId, split->manifestId(), auditSamples, auditConfig );
    REQUIRE( report.has_value() );
    const bool pairCrossings = std::any_of(
        report->findings().cbegin(), report->findings().cend(),
        []( const LeakageFinding &finding ) {
            return finding.kind == LeakageKind::PrePostPairLeakage;
        } );
    CHECK( pairCrossings );

    // The scientifically sound alternative: grouped-by-event split → clean.
    SplitConfig grouped;
    grouped.method = SplitMethod::Grouped;
    grouped.seed = 11;
    QVector<SplitInput> groupedInputs = inputs;
    for ( int i = 0; i < groupedInputs.size(); ++i )
        groupedInputs[i].groupId = groupedInputs[i].eventGroup;
    const auto groupedSplit =
        SplitEngine::generate( grouped, scenario.versionId, groupedInputs );
    REQUIRE( groupedSplit.has_value() );
    QVector<AuditSample> groupedAudit;
    for ( int i = 0; i < groupedInputs.size(); ++i )
    {
        AuditSample sample;
        sample.input = groupedInputs[i];
        sample.pairCounterpartId =
            i % 2 == 0 ? groupedInputs[i + 1].sampleId : groupedInputs[i - 1].sampleId;
        const auto assignment = groupedSplit->assignmentOf( groupedInputs[i].sampleId );
        sample.role = assignment ? assignment->role : SplitRole::Unassigned;
        groupedAudit.append( sample );
    }
    const auto cleanReport = LeakageAuditor::audit( scenario.versionId,
                                                    groupedSplit->manifestId(), groupedAudit,
                                                    auditConfig );
    REQUIRE( cleanReport.has_value() );
    const bool stillCrossing = std::any_of(
        cleanReport->findings().cbegin(), cleanReport->findings().cend(),
        []( const LeakageFinding &finding ) {
            return finding.kind == LeakageKind::PrePostPairLeakage;
        } );
    CHECK( !stillCrossing );
}

TEST_CASE( "Example C — optical+SAR temporal chain with missing observations",
           "[e2e][temporal]" )
{
    ScenarioStores scenario;
    scenario.open();
    scenario.commitVersion( QUuid::createUuid().toString( QUuid::WithoutBraces ),
                            QStringLiteral( "phenology" ), QStringLiteral( "timeseries" ),
                            QStringLiteral( "EPSG:4326" ) );

    // One temporal sample with a KNOWN missing observation; members of a
    // multimodal sample reference it per modality.
    SampleRecord temporal;
    temporal.setSampleId( SampleId::generate().toString() );
    temporal.setDatasetVersionId( scenario.versionId );
    temporal.setKind( SampleKind::Temporal );
    temporal.setGroupId( QStringLiteral( "field-1" ) );
    TemporalSample payload;
    const QStringList times = { QStringLiteral( "2025-04-01T00:00:00.000Z" ),
                                QStringLiteral( "2025-05-01T00:00:00.000Z" ),
                                QStringLiteral( "2025-06-01T00:00:00.000Z" ) };
    for ( int i = 0; i < 3; ++i )
    {
        TemporalObservation observation;
        observation.timeUtc = QDateTime::fromString( times.at( i ), Qt::ISODateWithMs );
        observation.missing = ( i == 1 ); // May acquisition lost
        if ( !observation.missing )
            observation.assetId = QStringLiteral( "asset-optical-%1" ).arg( i );
        payload.observations.append( observation );
    }
    payload.targetTimeUtc =
        QDateTime::fromString( QStringLiteral( "2025-07-01T00:00:00.000Z" ), Qt::ISODateWithMs );
    temporal.payload() = payload;
    REQUIRE( validateSample( temporal ).has_value() );
    REQUIRE( scenario.datasets.addSamples( QVector<SampleRecord>{ temporal } ).has_value() );

    // Multimodal sample referencing optical + SAR members with an explicit
    // missing policy for SAR.
    SampleRecord optical;
    optical.setSampleId( SampleId::generate().toString() );
    optical.setDatasetVersionId( scenario.versionId );
    optical.setKind( SampleKind::Pixel );
    PixelSample opticalPayload;
    opticalPayload.column = 5;
    opticalPayload.row = 5;
    optical.payload() = opticalPayload;
    SampleRecord sar = optical;
    sar.setSampleId( SampleId::generate().toString() );
    sar.setGroupId( QStringLiteral( "field-1" ) );
    REQUIRE( scenario.datasets
                 .addSamples( QVector<SampleRecord>{ optical, sar } )
                 .has_value() );

    SampleRecord multi;
    multi.setSampleId( SampleId::generate().toString() );
    multi.setDatasetVersionId( scenario.versionId );
    multi.setKind( SampleKind::MultiModal );
    MultiModalSample multiPayload;
    multiPayload.members = {
        { QStringLiteral( "optical" ), optical.sampleId(), true, QStringLiteral( "error" ) },
        { QStringLiteral( "sar" ), sar.sampleId(), false, QStringLiteral( "mask" ) },
    };
    multi.payload() = multiPayload;
    REQUIRE( validateSample( multi ).has_value() );
    REQUIRE( scenario.datasets.addSamples( QVector<SampleRecord>{ multi } ).has_value() );

    // Missing-observation policy is explicit in the payload: May is absent
    // but RECORDED (the hole is information).
    const auto stored = scenario.datasets.sampleById(
        DatasetVersionId::fromString( scenario.versionId ).value_or( DatasetVersionId{} ),
        SampleId::fromString( temporal.sampleId() ).value_or( SampleId{} ) );
    REQUIRE( stored.has_value() );
    const auto &roundTripped = std::get<TemporalSample>( stored->payload() );
    REQUIRE( roundTripped.observations.size() == 3 );
    CHECK( roundTripped.observations.at( 1 ).missing );
}

TEST_CASE( "Example D — segmentation chain with polygon labels and QA",
           "[e2e][segmentation]" )
{
    ScenarioStores scenario;
    scenario.open();
    scenario.commitVersion( QUuid::createUuid().toString( QUuid::WithoutBraces ),
                            QStringLiteral( "segmentation" ), QStringLiteral( "optical" ),
                            QStringLiteral( "EPSG:32650" ) );

    const LabelSchema schema = [] {
        LabelSchema schema;
        schema.setSchemaId( LabelSchemaId::generate().toString() );
        schema.setVersion( 1 );
        LabelClass building;
        building.setStableId( LabelSchemaId::generate().toString() );
        building.setCode( QStringLiteral( "building" ) );
        LabelClass background;
        background.setStableId( LabelSchemaId::generate().toString() );
        background.setCode( QStringLiteral( "background" ) );
        background.setBackground( true );
        background.setIgnore( true );
        schema.classes() = { building, background };
        return schema;
    }();
    REQUIRE( schema.validate().has_value() );

    // Polygon samples as segmentation labels.
    SampleRecord polygon;
    polygon.setSampleId( SampleId::generate().toString() );
    polygon.setDatasetVersionId( scenario.versionId );
    polygon.setKind( SampleKind::Polygon );
    polygon.setCrs( QStringLiteral( "EPSG:32650" ) );
    PolygonSample polygonPayload;
    polygonPayload.wkt = QStringLiteral( "POLYGON((0 0, 100 0, 100 100, 0 100, 0 0))" );
    polygon.payload() = polygonPayload;
    REQUIRE( validateSample( polygon ).has_value() );
    REQUIRE( scenario.datasets.addSamples( QVector<SampleRecord>{ polygon } ).has_value() );

    // Annotation on the polygon + label QA.
    AnnotationRecord annotation;
    annotation.setAnnotationId( AnnotationId::generate().toString() );
    annotation.setTargetSampleId( polygon.sampleId() );
    annotation.setDatasetVersionId( scenario.versionId );
    annotation.setRevision( 1 );
    annotation.setLabelSchemaId( schema.schemaId() );
    annotation.setLabelSchemaVersion( schema.version() );
    annotation.setClassCode( QStringLiteral( "building" ) );
    annotation.setSourceType( AnnotationSourceType::ManualInterpretation );
    REQUIRE( scenario.datasets.addAnnotation( annotation ).has_value() );

    LabelQaItem item;
    item.sampleId = polygon.sampleId();
    item.annotationIds = { annotation.annotationId() };
    item.classCodes = { QStringLiteral( "building" ) };
    item.geometryWkts = { polygonPayload.wkt };
    const auto findings = labelQualityAudit( { item }, schema, LabelQualityConfig{} );
    CHECK( findings.isEmpty() );
    CHECK( recommendQualityLevel( findings ) == DatasetQualityLevel::Certified );

    // Pixel metrics from the segmentation confusion matrix (known answer):
    // building: TP=90, predicted total 90, truth total 100 → IoU 0.9,
    // pixel accuracy 0.9.
    ConfusionMatrix matrix( { QStringLiteral( "building" ), QStringLiteral( "background" ) },
                            2 );
    matrix.setCount( 0, 0, 90 );
    matrix.setCount( 0, 1, 10 );
    matrix.setCount( 1, 0, 0 );
    matrix.setCount( 1, 1, 0 );
    CHECK( std::abs( matrix.perClass( 0 ).iou - 0.9 ) < 1e-12 );
    CHECK( std::abs( matrix.overallAccuracy() - 0.9 ) < 1e-12 );
}
