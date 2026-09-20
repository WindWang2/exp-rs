// test_data_foundation_scale.cpp — 12.0 data-foundation scale Oracle (O1):
// a 100,000-sample catalog and a 10,000-run experiment store must ingest in
// batch transactions, page via keyset cursors with bounded memory (page
// size ≤ 500 rows, no whole-table materialization), and answer indexed
// identity lookups — with a structured JSON performance report emitted for
// review. RUN_SERIAL: the benchmark must not compete for memory.
//
// The assertions are correctness + generous wall-clock ceilings; the JSON
// evidence carries the precise numbers a human reviewer reasons about.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "dataset/dataset_manifest.h"
#include "dataset/sample.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

TEST_CASE( "100k catalog + 10k runs: batch ingest, cursor paging, indexed "
           "identity lookup stay bounded",
           "[scale][data-foundation]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "scale.db" ) ) ) );

    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "scale-catalog" ) ).has_value() );
    const DatasetVersionId versionId = DatasetVersionId::generate();
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId.toString() );
    manifest.setVersionId( versionId.toString() );
    manifest.setName( QStringLiteral( "scale" ) );
    manifest.setCreatedAtUtc( QDateTime::fromString(
        QStringLiteral( "2026-09-20T00:00:00.000Z" ), Qt::ISODateWithMs ) );
    REQUIRE( store.createDraftVersion( manifest ).has_value() );

    constexpr int kTotalSamples = 100000;
    constexpr int kBatchSize = 5000;
    constexpr int kPageSize = 500;

    QJsonObject report;
    report.insert( QStringLiteral( "schema" ), QStringLiteral( "data-foundation-perf/1" ) );
    report.insert( QStringLiteral( "samples" ), static_cast<double>( kTotalSamples ) );
    report.insert( QStringLiteral( "page_size" ), static_cast<double>( kPageSize ) );

    // --- batch ingest (one transaction per batch) --------------------------
    QElapsedTimer timer;
    timer.start();
    for ( int batchStart = 0; batchStart < kTotalSamples; batchStart += kBatchSize )
    {
        QVector<SampleRecord> batch;
        batch.reserve( kBatchSize );
        for ( int i = batchStart; i < batchStart + kBatchSize; ++i )
        {
            SampleRecord sample;
            sample.setSampleId( SampleId::generate().toString() );
            sample.setDatasetVersionId( versionId.toString() );
            sample.setKind( SampleKind::Point );
            sample.setGroupId( QStringLiteral( "g%1" ).arg( i % 500, 3, 10, QLatin1Char( '0' ) ) );
            PointSample point;
            point.x = i % 10000;
            point.y = i / 10000;
            sample.payload() = point;
            sample.provenance()[QStringLiteral( "scene_id" )] =
                QStringLiteral( "scene-%1" ).arg( i % 40 );
            batch.append( sample );
        }
        REQUIRE( store.addSamples( batch ).has_value() );
    }
    const qint64 ingestMs = timer.elapsed();
    report.insert( QStringLiteral( "ingest_batches" ),
                   static_cast<double>( kTotalSamples / kBatchSize ) );
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "elapsed_ms" ), static_cast<double>( ingestMs ) );
        report.insert( QStringLiteral( "batch_ingest" ), entry );
    }
    CHECK( store.sampleCount( versionId ) == kTotalSamples );

    // --- keyset cursor walk: every row exactly once, bounded pages ---------
    timer.restart();
    QString cursor;
    QString lastFullPageCursor; // resume point of the final full page
    int pages = 0;
    qint64 visited = 0;
    QSet<QString> seenIds;
    seenIds.reserve( kTotalSamples );
    while ( true )
    {
        const auto page = store.samplesPageCursor( versionId, cursor, kPageSize );
        REQUIRE( page.has_value() );
        CHECK( page->samples.size() <= kPageSize );
        CHECK( page->total == kTotalSamples );
        if ( page->samples.size() == kPageSize )
            lastFullPageCursor = cursor;
        for ( const SampleRecord &sample : page->samples )
            seenIds.insert( sample.sampleId() );
        visited += page->samples.size();
        ++pages;
        REQUIRE( pages <= kTotalSamples / kPageSize + 2 );
        if ( page->nextCursor.isEmpty() )
            break;
        cursor = page->nextCursor;
    }
    const qint64 cursorWalkMs = timer.elapsed();
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "elapsed_ms" ), static_cast<double>( cursorWalkMs ) );
        entry.insert( QStringLiteral( "pages" ), static_cast<double>( pages ) );
        entry.insert( QStringLiteral( "visited" ), static_cast<double>( visited ) );
        report.insert( QStringLiteral( "cursor_walk" ), entry );
    }
    CHECK( visited == kTotalSamples );
    CHECK( seenIds.size() == kTotalSamples );

    // --- deep-offset probe: correctness first, cost recorded ---------------
    timer.restart();
    const auto deepPage = store.samplesPage( versionId, kTotalSamples - kPageSize, kPageSize );
    const qint64 deepOffsetMs = timer.elapsed();
    REQUIRE( deepPage.has_value() );
    CHECK( deepPage->second.size() == kPageSize );
    timer.restart();
    const auto lastCursorPage = store.samplesPageCursor( versionId, lastFullPageCursor, kPageSize );
    const qint64 lastCursorMs = timer.elapsed();
    REQUIRE( lastCursorPage.has_value() );
    CHECK( lastCursorPage->samples.size() == kPageSize );
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "deep_offset_ms" ), static_cast<double>( deepOffsetMs ) );
        entry.insert( QStringLiteral( "last_cursor_page_ms" ),
                      static_cast<double>( lastCursorMs ) );
        report.insert( QStringLiteral( "deep_page_probe" ), entry );
    }
    CHECK( lastCursorMs < 5000 ); // generous: keyset pages never regress to scans

    // --- bounded DISTINCT query over the full catalog -----------------------
    timer.restart();
    const QVector<QString> groups = store.sampleGroupIds( versionId, 10000 );
    const qint64 groupQueryMs = timer.elapsed();
    CHECK( groups.size() == 500 );
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "elapsed_ms" ), static_cast<double>( groupQueryMs ) );
        entry.insert( QStringLiteral( "distinct_groups" ),
                      static_cast<double>( groups.size() ) );
        report.insert( QStringLiteral( "group_distinct_query" ), entry );
    }

    // --- experiment store: 10k runs, batch upsert + cursor paging ----------
    ExperimentStore experimentStore;
    REQUIRE( experimentStore.open( dir.filePath( QStringLiteral( "scale_exp.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "scale-runs" ) );
    REQUIRE( experimentStore.upsertExperiment( experiment ).has_value() );

    constexpr int kTotalRuns = 10000;
    constexpr int kRunBatch = 500;
    RunExecutionIdentity identity;
    identity.algorithmId = QStringLiteral( "rs:classify" );
    identity.algorithmVersion = QStringLiteral( "1.0" );
    identity.datasetVersionId = versionId.toString();
    identity.seed = 42;
    timer.restart();
    for ( int batchStart = 0; batchStart < kTotalRuns; batchStart += kRunBatch )
    {
        QVector<ExperimentRun> batch;
        batch.reserve( kRunBatch );
        for ( int i = batchStart; i < batchStart + kRunBatch; ++i )
        {
            ExperimentRun run;
            run.setRunId( QStringLiteral( "run-%1" ).arg( i, 5, 10, QLatin1Char( '0' ) ) );
            run.setExperimentId( experiment.experimentId() );
            run.setAlgorithmId( identity.algorithmId );
            run.setAlgorithmVersion( identity.algorithmVersion );
            run.setDatasetVersionId( identity.datasetVersionId );
            run.setSeed( identity.seed );
            run.setStatus( RunStatus::Created );
            batch.append( run );
        }
        REQUIRE( experimentStore.upsertRunsBatch( batch ).has_value() );
    }
    const qint64 runIngestMs = timer.elapsed();
    CHECK( experimentStore.runCount() == kTotalRuns );
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "elapsed_ms" ), static_cast<double>( runIngestMs ) );
        entry.insert( QStringLiteral( "runs" ), static_cast<double>( kTotalRuns ) );
        report.insert( QStringLiteral( "run_batch_ingest" ), entry );
    }

    timer.restart();
    QString runCursor;
    qint64 runsVisited = 0;
    QSet<QString> seenRunIds;
    seenRunIds.reserve( kTotalRuns );
    int runPages = 0;
    while ( true )
    {
        const auto page = experimentStore.listRunsByCursor(
            experiment.experimentId(), QString(), QString(), runCursor, kPageSize );
        REQUIRE( page.has_value() );
        CHECK( page->total == kTotalRuns );
        CHECK( page->runs.size() <= kPageSize );
        for ( const ExperimentRun &run : page->runs )
            seenRunIds.insert( run.runId() );
        runsVisited += page->runs.size();
        ++runPages;
        REQUIRE( runPages <= kTotalRuns / kPageSize + 2 );
        if ( page->nextCursor.isEmpty() )
            break;
        runCursor = page->nextCursor;
    }
    const qint64 runWalkMs = timer.elapsed();
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "elapsed_ms" ), static_cast<double>( runWalkMs ) );
        entry.insert( QStringLiteral( "pages" ), static_cast<double>( runPages ) );
        entry.insert( QStringLiteral( "visited" ), static_cast<double>( runsVisited ) );
        report.insert( QStringLiteral( "run_cursor_walk" ), entry );
    }
    CHECK( runsVisited == kTotalRuns );
    CHECK( seenRunIds.size() == kTotalRuns );

    // --- indexed identity lookup --------------------------------------------
    timer.restart();
    const QStringList twinIds = experimentStore.runIdsByExecutionFingerprint(
        runExecutionFingerprint( identity ), ExperimentStore::kMaxPageSize );
    const qint64 fpLookupMs = timer.elapsed();
    CHECK( twinIds.size() == 500 ); // page-bound slice of the 10k identity twins
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "elapsed_ms" ), static_cast<double>( fpLookupMs ) );
        entry.insert( QStringLiteral( "matches" ), static_cast<double>( twinIds.size() ) );
        report.insert( QStringLiteral( "execution_fingerprint_lookup" ), entry );
    }
    CHECK( fpLookupMs < 2000 ); // indexed lookup, generous ceiling

    // --- emit the structured evidence ---------------------------------------
    const QByteArray evidenceDir = qgetenv( "SICNU_PERF_EVIDENCE_DIR" );
    if ( !evidenceDir.isEmpty() )
    {
        const QString path = QString::fromUtf8( evidenceDir ) +
                             QStringLiteral( "/data-foundation-perf.json" );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        file.write( QJsonDocument( report ).toJson( QJsonDocument::Indented ) );
    }
    INFO( "perf report: " << QJsonDocument( report ).toJson( QJsonDocument::Compact ).toStdString() );
    CHECK( ingestMs < 300000 );
    CHECK( cursorWalkMs < 120000 );
    CHECK( runIngestMs < 300000 );
    CHECK( runWalkMs < 60000 );
}
