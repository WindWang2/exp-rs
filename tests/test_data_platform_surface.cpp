// test_data_platform_surface.cpp — Platform 7.0 dataset/experiment MCP
// surface tests:
//   - split manifest persistence (immutability, idempotency, conflict)
//   - leakage report persistence (append-only, latest, split_not_found)
//   - the dataset:/experiment:/reproducibility: tool handlers over real
//     stores: bounded pages, truthful errors, honest readiness levels
#include <catch2/catch_test_macros.hpp>

#include "agent/data_platform_tools.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/leakage_audit.h"
#include "dataset/sample.h"
#include "dataset/split.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QVariantMap>

#include <stdexcept>

using namespace sicnu::dataset;

namespace
{

struct Fixture
{
    QTemporaryDir dir;
    QString datasetDb;
    QString experimentDb;

    Fixture()
    {
        datasetDb = dir.filePath( QStringLiteral( "dataset.db" ) );
        experimentDb = dir.filePath( QStringLiteral( "experiment.db" ) );
    }
};

// Builds a store with one committed version carrying @p count point samples
// split into two groups, plus a stored k-fold split manifest with leakage
// audit evidence.
struct SeededStore
{
    Fixture fixture;
    DatasetStore store;
    QString versionId;
    QString manifestId;

    explicit SeededStore( int sampleCount = 20 )
    {
        REQUIRE( store.open( fixture.datasetDb ) );
        const auto datasetId = DatasetId::generate();
        REQUIRE( store.createDataset( datasetId, QStringLiteral( "seeded" ) ).has_value() );
        const auto newVersionId = DatasetVersionId::generate();
        DatasetManifest manifest;
        manifest.setDatasetId( datasetId.toString() );
        manifest.setVersionId( newVersionId.toString() );
        manifest.setName( QStringLiteral( "seeded" ) );
        manifest.setCreatedAtUtc( QDateTime::fromString(
            QStringLiteral( "2026-09-09T00:00:00.000Z" ), Qt::ISODateWithMs ) );
        const auto version = store.createDraftVersion( manifest );
        REQUIRE( version.has_value() );
        versionId = version.value().versionId();

        QVector<SampleRecord> samples;
        for ( int i = 0; i < sampleCount; ++i )
        {
            SampleRecord sample;
            sample.setSampleId(
                SampleId::generate().toString() );
            sample.setDatasetVersionId( versionId );
            sample.setKind( SampleKind::Point );
            sample.setGroupId( i % 2 == 0 ? QStringLiteral( "group-a" ) : QStringLiteral( "group-b" ) );
            PointSample point;
            point.x = i;
            point.y = i * 2;
            sample.payload() = point;
            sample.provenance()[QStringLiteral( "scene_id" )] =
                i % 4 == 0 ? QStringLiteral( "scene-1" ) : QStringLiteral( "scene-2" );
            sample.provenance()[QStringLiteral( "content_digest" )] =
                QStringLiteral( "digest-%1" ).arg( i % 5 ); // planted duplicates
            samples.append( sample );
        }
        REQUIRE( store.addSamples( samples ).has_value() );

        SplitConfig config;
        config.method = SplitMethod::KFold;
        config.seed = 42;
        config.foldCount = 4;
        QVector<SplitInput> inputs;
        for ( const auto &sample : samples )
        {
            SplitInput input;
            input.sampleId = sample.sampleId();
            input.groupId = sample.groupId();
            inputs.append( input );
        }
        const auto generated = SplitEngine::generate( config, versionId, inputs );
        REQUIRE( generated.has_value() );
        manifestId = generated.value().manifestId();
        REQUIRE( store.saveSplitManifest( generated.value() ).has_value() );
        const auto commitId = DatasetVersionId::fromString( version.value().versionId() ).value();
        REQUIRE( store.stageVersion( commitId ).has_value() );
        REQUIRE( store.commitVersion( commitId ).has_value() );
    }
};

QVariantMap baseArgs( const QString &datasetDb )
{
    QVariantMap args;
    args.insert( QStringLiteral( "dataset_db" ), datasetDb );
    return args;
}

QVariantMap variantArgs( const QString &experimentDb )
{
    QVariantMap args;
    args.insert( QStringLiteral( "experiment_db" ), experimentDb );
    return args;
}

} // namespace

TEST_CASE( "split manifest persistence is immutable and idempotent", "[dataset][store][splits]" )
{
    SeededStore seeded;
    const auto manifest = seeded.store.splitManifestById( seeded.manifestId );
    REQUIRE( manifest.has_value() );
    REQUIRE( manifest->manifestId() == seeded.manifestId );
    REQUIRE( manifest->fingerprint() ==
             splitManifestFingerprint( manifest.value() ) );

    // Idempotent re-save of identical content.
    REQUIRE( seeded.store.saveSplitManifest( manifest.value() ).has_value() );

    // Re-saving the same id with different content is a conflict.
    auto mutated = manifest.value();
    mutated.setNote( QStringLiteral( "tampered" ) );
    const auto conflict = seeded.store.saveSplitManifest( mutated );
    REQUIRE( !conflict.has_value() );
    REQUIRE( conflict.diagnostics().first().code == QStringLiteral( "dataset.conflict" ) );

    // Loading a missing manifest reads as absent.
    REQUIRE( !seeded.store.splitManifestById( QStringLiteral( "nope" ) ).has_value() );

    // Listed under its version.
    const auto listed = seeded.store.splitManifestsForVersion(
        DatasetVersionId::fromString( manifest->datasetVersionId() ).value() );
    REQUIRE( listed.size() == 1 );
    REQUIRE( listed.first().manifestId() == seeded.manifestId );
}

TEST_CASE( "leakage reports are append-only and keyed by manifest", "[dataset][store][leakage]" )
{
    SeededStore seeded;

    LeakageAuditConfig config;
    config.checks = QStringList{ QStringLiteral( "exact_duplicate" ) };
    const auto manifest = seeded.store.splitManifestById( seeded.manifestId );
    REQUIRE( manifest.has_value() );

    // Assemble minimal audit samples (identity + role/fold from the manifest).
    QVector<AuditSample> samples;
    QHash<QString, QPair<SplitRole, int>> placement;
    for ( const auto &assignment : manifest->assignments() )
        placement[assignment.sampleId] = qMakePair( assignment.role, assignment.fold );
    const auto page = seeded.store.samplesPage(
        DatasetVersionId::fromString( manifest->datasetVersionId() ).value(), 0, 500 );
    REQUIRE( page.has_value() );
    for ( const auto &sample : page.value().second )
    {
        AuditSample item;
        item.input.sampleId = sample.sampleId();
        item.input.groupId = sample.groupId();
        const auto it = placement.constFind( sample.sampleId() );
        if ( it != placement.constEnd() )
        {
            item.role = it->first;
            item.fold = it->second;
        }
        item.contentDigest = sample.provenance().value( QStringLiteral( "content_digest" ) ).toString();
        samples.append( item );
    }
    const auto report = LeakageAuditor::audit( manifest->datasetVersionId(),
                                               seeded.manifestId, samples, config );
    REQUIRE( report.has_value() );
    CHECK( !report->isClean() ); // digest-%1 mod 5 duplicates planted
    CHECK( report->auditedChecks().contains( QStringLiteral( "exact_duplicate" ) ) );

    REQUIRE( seeded.store.saveLeakageReport( report.value() ).has_value() );
    // Identical content re-save is idempotent (append-only by digest).
    REQUIRE( seeded.store.saveLeakageReport( report.value() ).has_value() );

    const auto latest = seeded.store.latestLeakageReport( seeded.manifestId );
    REQUIRE( latest.has_value() );
    CHECK( latest->summary() == report->summary() );
    CHECK( latest->sampleCount() == report->sampleCount() ); // round-trip gap
    CHECK( latest->digestUnknownCount() == report->digestUnknownCount() );

    // Reports of a version list under the manifest.
    const auto reports = seeded.store.leakageReportsForSplit( seeded.manifestId );
    CHECK( reports.size() == 1 );

    // A report cannot attach to a manifest that is not stored.
    LeakageReport orphan;
    orphan.setSplitManifestId( QStringLiteral( "missing-manifest" ) );
    const auto rejected = seeded.store.saveLeakageReport( orphan );
    REQUIRE( !rejected.has_value() );
    REQUIRE( rejected.diagnostics().first().code == QStringLiteral( "dataset.split_not_found" ) );
}

TEST_CASE( "dataset: tools expose bounded, truthful projections",
           "[agent][mcp][data_platform]" )
{
    using sicnu::agent::handleDataPlatformTool;
    using sicnu::agent::isDataPlatformTool;
    SeededStore seeded;
    const QString versionId = seeded.versionId;

    CHECK( isDataPlatformTool( QStringLiteral( "dataset:list" ) ) );
    CHECK( isDataPlatformTool( QStringLiteral( "reproducibility:export" ) ) );
    CHECK( !isDataPlatformTool( QStringLiteral( "data:list_layers" ) ) );

    // dataset:list — bounded page with total + next_cursor.
    QVariantMap listArgs = baseArgs( seeded.fixture.datasetDb );
    listArgs.insert( QStringLiteral( "limit" ), 2 );
    auto listed = handleDataPlatformTool( QStringLiteral( "dataset:list" ), listArgs );
    CHECK( listed.value( QStringLiteral( "total" ) ).toLongLong() == 1 );
    CHECK( listed.value( QStringLiteral( "next_cursor" ) ).toLongLong() == -1 );

    // Over-large limit clamps; cursor past the end is an empty page.
    listArgs.insert( QStringLiteral( "limit" ), 99999 );
    listArgs.insert( QStringLiteral( "cursor" ), 5 );
    listed = handleDataPlatformTool( QStringLiteral( "dataset:list" ), listArgs );
    CHECK( listed.value( QStringLiteral( "datasets" ) ).toList().isEmpty() );

    // dataset:inspect on a version carries manifest + counts + splits.
    QVariantMap inspectArgs = baseArgs( seeded.fixture.datasetDb );
    inspectArgs.insert( QStringLiteral( "version" ), versionId );
    auto inspected = handleDataPlatformTool( QStringLiteral( "dataset:inspect" ), inspectArgs );
    CHECK( inspected.value( QStringLiteral( "sample_count" ) ).toLongLong() == 20 );
    CHECK( inspected.value( QStringLiteral( "split_manifests" ) ).toList().size() == 1 );

    // Unknown version is a typed error, not a crash.
    inspectArgs[QStringLiteral( "version" )] = QStringLiteral( "not-a-uuid" );
    REQUIRE_THROWS_AS( handleDataPlatformTool( QStringLiteral( "dataset:inspect" ), inspectArgs ),
                       std::runtime_error );

    // dataset:stats — kind/group buckets, bounded output.
    QVariantMap statsArgs = baseArgs( seeded.fixture.datasetDb );
    statsArgs.insert( QStringLiteral( "version" ), versionId );
    auto stats = handleDataPlatformTool( QStringLiteral( "dataset:stats" ), statsArgs );
    CHECK( stats.value( QStringLiteral( "sample_count" ) ).toLongLong() == 20 );
    CHECK( stats.value( QStringLiteral( "by_group" ) ).toMap().size() == 2 );

    // dataset:validate is read-only: the committed version's stored manifest
    // parses, so valid=true (and no state change is needed to answer).
    auto validated = handleDataPlatformTool( QStringLiteral( "dataset:validate" ), statsArgs );
    CHECK( validated.value( QStringLiteral( "valid" ) ).toBool() );
    CHECK( validated.value( QStringLiteral( "status" ) ).toString()
           == QStringLiteral( "committed" ) );

    // dataset:split_inspect summary carries role/fold distribution.
    QVariantMap splitArgs = baseArgs( seeded.fixture.datasetDb );
    splitArgs.insert( QStringLiteral( "split_manifest_id" ), seeded.manifestId );
    splitArgs.insert( QStringLiteral( "limit" ), 5 );
    auto split = handleDataPlatformTool( QStringLiteral( "dataset:split_inspect" ), splitArgs );
    CHECK( split.value( QStringLiteral( "fold_counts" ) ).toMap().size() == 4 );
    CHECK( split.value( QStringLiteral( "assignments" ) ).toList().size() == 5 );
    CHECK( split.value( QStringLiteral( "assignment_total" ) ).toLongLong() == 20 );
    CHECK( split.value( QStringLiteral( "next_cursor" ) ).toLongLong() == 5 );

    // dataset:leakage_audit run mode persists a report; stored mode reads it.
    QVariantMap auditArgs = baseArgs( seeded.fixture.datasetDb );
    auditArgs.insert( QStringLiteral( "split_manifest_id" ), seeded.manifestId );
    auditArgs.insert( QStringLiteral( "mode" ), QStringLiteral( "run" ) );
    auto audit = handleDataPlatformTool( QStringLiteral( "dataset:leakage_audit" ), auditArgs );
    CHECK( audit.value( QStringLiteral( "persisted" ) ).toBool() );
    const auto reportJson = audit.value( QStringLiteral( "report" ) ).toMap();
    CHECK( !reportJson.value( QStringLiteral( "findings" ) ).toList().isEmpty() );

    auditArgs.insert( QStringLiteral( "mode" ), QStringLiteral( "stored" ) );
    auto stored = handleDataPlatformTool( QStringLiteral( "dataset:leakage_audit" ), auditArgs );
    CHECK( stored.contains( QStringLiteral( "report" ) ) );

    // dataset:diff across the single version errors truthfully (same dataset
    // required, both versions must exist).
    QVariantMap diffArgs = baseArgs( seeded.fixture.datasetDb );
    diffArgs.insert( QStringLiteral( "from" ), versionId );
    diffArgs.insert( QStringLiteral( "to" ), QStringLiteral( "00000000-0000-4000-8000-000000000000" ) );
    REQUIRE_THROWS_AS( handleDataPlatformTool( QStringLiteral( "dataset:diff" ), diffArgs ),
                       std::runtime_error );
}

TEST_CASE( "experiment: tools inspect and compare runs", "[agent][mcp][data_platform]" )
{
    using sicnu::agent::handleDataPlatformTool;
    SeededStore seeded;

    sicnu::experiment::ExperimentStore experimentStore;
    REQUIRE( experimentStore.open( seeded.fixture.experimentDb ) );

    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "11111111-2222-4333-8444-555555555555" ) );
    experiment.setName( QStringLiteral( "fold study" ) );
    experiment.setCreatedAtUtc( QDateTime::fromString(
        QStringLiteral( "2026-09-09T00:00:00.000Z" ), Qt::ISODateWithMs ) );
    REQUIRE( experimentStore.upsertExperiment( experiment ).has_value() );

    sicnu::experiment::ExperimentRun run;
    run.setRunId( QStringLiteral( "aaaaaaaa-bbbb-4333-8444-555555555555" ) );
    run.setExperimentId( experiment.experimentId() );
    run.setStatus( RunStatus::Created );
    run.setAlgorithmId( QStringLiteral( "rs:spectral_index" ) );
    run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
    run.setDatasetVersionId( seeded.versionId );
    run.setSplitManifestId( seeded.manifestId );
    run.setSeed( 7 );
    run.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experimentStore.upsertRun( run ).has_value() );
    CHECK( experimentStore.runById( run.runId() ).has_value() ); // writer sees it

    QVariantMap listArgs = variantArgs( seeded.fixture.experimentDb );
    auto listed = handleDataPlatformTool( QStringLiteral( "experiment:list" ), listArgs );
    CHECK( listed.value( QStringLiteral( "experiments" ) ).toList().size() == 1 );

    QVariantMap inspectArgs = variantArgs( seeded.fixture.experimentDb );
    inspectArgs.insert( QStringLiteral( "run" ), run.runId() );
    auto inspected = handleDataPlatformTool( QStringLiteral( "experiment:inspect" ), inspectArgs );
    CHECK( inspected.value( QStringLiteral( "run_id" ) ).toString() == run.runId() );
    CHECK( inspected.contains( QStringLiteral( "execution_fingerprint" ) ) );

    // experiment:compare of a run with itself is Comparable with empty diffs.
    QVariantMap compareArgs = variantArgs( seeded.fixture.experimentDb );
    compareArgs.insert( QStringLiteral( "a" ), run.runId() );
    compareArgs.insert( QStringLiteral( "b" ), run.runId() );
    auto compared = handleDataPlatformTool( QStringLiteral( "experiment:compare" ), compareArgs );
    CHECK( compared.value( QStringLiteral( "verdict" ) ).toString() ==
           QStringLiteral( "comparable" ) );
    // Goal 8.0 §F: experiment identity + tags ride the comparison so
    // baseline/treatment grouping is visible.
    const QVariantMap experimentContext =
        compared.value( QStringLiteral( "experiment_context" ) ).toMap();
    CHECK( experimentContext.value( QStringLiteral( "a" ) ).toMap()
               .value( QStringLiteral( "experiment_id" ) )
               .toString() == run.experimentId() );
    CHECK( experimentContext.contains( QStringLiteral( "b" ) ) );
}

TEST_CASE( "reproducibility:inspect degrades honestly", "[agent][mcp][data_platform][repro]" )
{
    using sicnu::agent::handleDataPlatformTool;
    SeededStore seeded;

    sicnu::experiment::ExperimentStore experimentStore;
    REQUIRE( experimentStore.open( seeded.fixture.experimentDb ) );
    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "11111111-2222-4333-8444-555555555555" ) );
    experiment.setName( QStringLiteral( "repro" ) );
    experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experimentStore.upsertExperiment( experiment ).has_value() );
    const QString reproExperimentId = experiment.experimentId();

    sicnu::experiment::ExperimentRun run;
    run.setRunId( QStringLiteral( "cccccccc-dddd-4333-8444-555555555555" ) );
    run.setExperimentId( reproExperimentId );
    run.setAlgorithmId( QStringLiteral( "rs:spectral_index" ) );
    run.setDatasetVersionId( seeded.versionId );
    run.setSplitManifestId( seeded.manifestId );
    run.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );

    // Everything pinned and present, model/algorithm unknown → best_effort.
    REQUIRE( experimentStore.upsertRun( run ).has_value() );
    QVariantMap args = baseArgs( seeded.fixture.datasetDb );
    args.insert( QStringLiteral( "experiment_db" ), seeded.fixture.experimentDb );
    args.insert( QStringLiteral( "run" ), run.runId() );
    auto ready = handleDataPlatformTool( QStringLiteral( "reproducibility:inspect" ), args );
    CHECK( ready.value( QStringLiteral( "level" ) ).toString() == QStringLiteral( "best_effort" ) );

    // Unpinned dataset/split → impossible with missing diagnostics.
    sicnu::experiment::ExperimentRun bare;
    bare.setRunId( QStringLiteral( "dddddddd-eeee-4333-8444-555555555555" ) );
    bare.setExperimentId( reproExperimentId );
    bare.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experimentStore.upsertRun( bare ).has_value() );
    args[QStringLiteral( "run" )] = bare.runId();
    auto notReady = handleDataPlatformTool( QStringLiteral( "reproducibility:inspect" ), args );
    CHECK( notReady.value( QStringLiteral( "level" ) ).toString() == QStringLiteral( "impossible" ) );

    // A pinned-but-absent version is missing, never faked as ok.
    sicnu::experiment::ExperimentRun ghost;
    ghost.setRunId( QStringLiteral( "eeeeeeee-ffff-4333-8444-555555555555" ) );
    ghost.setExperimentId( reproExperimentId );
    ghost.setDatasetVersionId( QStringLiteral( "99999999-8888-4777-8666-555555555555" ) );
    ghost.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experimentStore.upsertRun( ghost ).has_value() );
    args[QStringLiteral( "run" )] = ghost.runId();
    auto missing = handleDataPlatformTool( QStringLiteral( "reproducibility:inspect" ), args );
    CHECK( missing.value( QStringLiteral( "level" ) ).toString() == QStringLiteral( "impossible" ) );
    bool sawMissing = false;
    for ( const auto &check : missing.value( QStringLiteral( "checks" ) ).toList() )
        if ( check.toMap().value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "missing" ) )
            sawMissing = true;
    CHECK( sawMissing );
}
