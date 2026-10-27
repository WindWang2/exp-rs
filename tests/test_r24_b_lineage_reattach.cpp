// tests/test_r24_b_lineage_reattach.cpp — R24-B (#1581 residual closure,
// WP-B7/B8): reproduction bundles export DIRECTION-PRESERVING lineage
// slices and import re-attaches them (run endpoint rewritten to the
// installed id), de-islanding the imported run in the lineage graph.
// Covers: outgoing+incoming direction, fresh-id rewrite, legacy 3-key
// bundles keep the named-gap warning, hostile items are skipped with named
// warnings (bad kind / foreign run / self-edge / oversize id), dataset-side
// endpoints need the optional DatasetStore, and the re-attached graph is
// traversable end to end.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_ids.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/lineage.h"
#include "experiment/reproduction_bundle.h"
#include "experiment/reproduction_bundle_import.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace {

ExperimentRun makeRun( const QString &runId, const QString &experimentId )
{
    ExperimentRun run;
    run.setRunId( runId );
    run.setExperimentId( experimentId );
    run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
    run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
    QJsonObject parameters;
    parameters.insert( QStringLiteral( "bands" ), QJsonArray{ 1, 2, 3 } );
    parameters.insert( QStringLiteral( "model" ), QStringLiteral( "rf" ) );
    run.setParameters( parameters );
    run.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    run.setDatasetFingerprint( QStringLiteral( "df1" ) );
    run.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    run.setSplitFingerprint( QStringLiteral( "sf1" ) );
    run.setSeed( 42 );
    return run;
}

struct Fixture
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    QString committedVersionId;
    QString committedFingerprint;

    Fixture()
    {
        REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
        REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

        // Real committed version so the exported run pins existing refs
        // (same shape as the historical bundle tests).
        const DatasetId datasetId = DatasetId::generate();
        REQUIRE( datasets.createDataset( datasetId, QStringLiteral( "lc" ) ).has_value() );
        DatasetManifest manifest;
        manifest.setDatasetId( datasetId.toString() );
        manifest.setVersionId( DatasetVersionId::generate().toString() );
        const auto draft = datasets.createDraftVersion( manifest );
        REQUIRE( draft.has_value() );
        const DatasetVersionId versionId =
            DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} );
        REQUIRE( datasets.stageVersion( versionId ).has_value() );
        const auto committed = datasets.commitVersion( versionId );
        REQUIRE( committed.has_value() );
        committedVersionId = committed->versionId();
        committedFingerprint = committed->fingerprint();

        Experiment experiment;
        experiment.setExperimentId( QStringLiteral( "exp-src" ) );
        experiment.setName( QStringLiteral( "source" ) );
        REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

        ExperimentRun run = makeRun( QStringLiteral( "run-src" ), QStringLiteral( "exp-src" ) );
        run.setDatasetVersionId( committedVersionId );
        run.setDatasetFingerprint( committedFingerprint );
        run.setStatus( RunStatus::Completed );
        run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
        REQUIRE( experiments.upsertRun( run ).has_value() );

        // One outgoing (run produced artifact) + one incoming (experiment
        // produced run) experiment-side edge, and one dataset-side edge.
        REQUIRE( experiments
                     .addLineageEdge( QStringLiteral( "run" ), QStringLiteral( "run-src" ),
                                      QStringLiteral( "produced" ),
                                      QStringLiteral( "artifact" ),
                                      QStringLiteral( "ndvi.tif" ) )
                     .has_value() );
        REQUIRE( experiments
                     .addLineageEdge( QStringLiteral( "experiment" ), QStringLiteral( "exp-src" ),
                                      QStringLiteral( "produced" ), QStringLiteral( "run" ),
                                      QStringLiteral( "run-src" ) )
                     .has_value() );
    }

    ReproductionBundleReport exportBundle( const QString &name )
    {
        ReproductionBundleExporter exporter( experiments, datasets );
        ReproductionBundleOptions options;
        options.outputDir = dir.filePath( name );
        options.currentSoftwareRevision = QStringLiteral( "r24-b" );
        return exporter.exportRun( QStringLiteral( "run-src" ), options );
    }
};

QJsonObject readProvenance( const QString &bundlePath )
{
    QFile file( QDir( bundlePath ).filePath( QStringLiteral( "provenance.json" ) ) );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return QJsonDocument::fromJson( file.readAll() ).object();
}

void writeProvenanceAndResign( const QString &bundlePath, const QJsonObject &provenance )
{
    const QByteArray bytes = QJsonDocument( provenance ).toJson( QJsonDocument::Indented );
    const QString provenancePath =
        QDir( bundlePath ).filePath( QStringLiteral( "provenance.json" ) );
    REQUIRE( QFile::remove( provenancePath ) );
    {
        QFile out( provenancePath );
        REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        REQUIRE( out.write( bytes ) == bytes.size() );
    }
    const QString checksumsPath =
        QDir( bundlePath ).filePath( QStringLiteral( "checksums.txt" ) );
    QFile checksumsFile( checksumsPath );
    REQUIRE( checksumsFile.open( QIODevice::ReadOnly ) );
    QStringList lines = QString::fromUtf8( checksumsFile.readAll() )
                            .split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
    checksumsFile.close();
    const QString digest =
        QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 )
                                 .toHex() );
    for ( QString &line : lines )
        if ( line.endsWith( QStringLiteral( "  provenance.json" ) ) )
            line = digest + QStringLiteral( "  provenance.json" );
    const QByteArray rewritten = lines.join( QLatin1Char( '\n' ) ).toUtf8() + "\n";
    REQUIRE( QFile::remove( checksumsPath ) );
    {
        QFile out( checksumsPath );
        REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        REQUIRE( out.write( rewritten ) == rewritten.size() );
    }
}

} // namespace

TEST_CASE( "bundle lineage slice is direction-preserving and re-attaches "
           "with a fresh run id (R24-B)",
           "[r24_b][experiment][repro][lineage]" )
{
    Fixture fx;
    const auto exportReport = fx.exportBundle( QStringLiteral( "bundle" ) );
    REQUIRE( exportReport.ok );

    // Export shape: every item carries from_*/to_* (and the legacy other_*).
    {
        const QJsonObject provenance = readProvenance( exportReport.bundlePath );
        const QJsonArray edges = provenance.value( QStringLiteral( "run_edges" ) ).toArray();
        REQUIRE( edges.size() == 2 );
        bool sawOutgoing = false, sawIncoming = false;
        for ( const QJsonValue &value : edges )
        {
            const QJsonObject item = value.toObject();
            REQUIRE( !item.value( QStringLiteral( "from_kind" ) ).toString().isEmpty() );
            REQUIRE( !item.value( QStringLiteral( "to_kind" ) ).toString().isEmpty() );
            if ( item.value( QStringLiteral( "from_kind" ) ).toString()
                     == QStringLiteral( "run" ) )
                sawOutgoing = true;
            if ( item.value( QStringLiteral( "to_kind" ) ).toString()
                     == QStringLiteral( "run" ) )
                sawIncoming = true;
        }
        CHECK( sawOutgoing );
        CHECK( sawIncoming );
    }

    ExperimentStore target;
    REQUIRE( target.open( fx.dir.filePath( QStringLiteral( "target.db" ) ) ) );
    Experiment home;
    home.setExperimentId( QStringLiteral( "exp-dst" ) );
    home.setName( QStringLiteral( "destination" ) );
    REQUIRE( target.upsertExperiment( home ).has_value() );

    // keepOriginalRunId=false → the imported run gets a FRESH id; the
    // re-attached edges must name that new id, not the bundle's original.
    ReproductionBundleImporter importer( target );
    ReproductionBundleImportOptions options;
    options.bundleDir = exportReport.bundlePath;
    options.targetExperimentId = QStringLiteral( "exp-dst" );
    const auto report = importer.importRun( options );

    REQUIRE( report.ok );
    CHECK( report.lineageEdgesReattached == 2 );
    CHECK( report.runId != QStringLiteral( "run-src" ) );

    const auto out = target.outgoingEdges( QStringLiteral( "run" ), report.runId );
    REQUIRE( out.size() == 1 );
    CHECK( out.first().fromId == report.runId );
    CHECK( out.first().edgeKind == QStringLiteral( "produced" ) );
    CHECK( out.first().toKind == QStringLiteral( "artifact" ) );
    CHECK( out.first().toId == QStringLiteral( "ndvi.tif" ) );

    const auto in = target.incomingEdges( QStringLiteral( "run" ), report.runId );
    REQUIRE( in.size() == 1 );
    CHECK( in.first().toId == report.runId );
    CHECK( in.first().fromKind == QStringLiteral( "experiment" ) );
    CHECK( in.first().fromId == QStringLiteral( "exp-src" ) );

    // No edges were attached under the ORIGINAL id (that would be an
    // island of its own — edges naming an id the store does not hold).
    CHECK( target.outgoingEdges( QStringLiteral( "run" ),
                                 QStringLiteral( "run-src" ) )
               .isEmpty() );

    // De-islanding, end to end: ancestors of the imported run reach the
    // pinned experiment node through the re-attached incoming edge.
    LineageGraph graph( fx.datasets, target );
    LineageQueryResult ancestors = graph.ancestors(
        { QStringLiteral( "run" ), report.runId } );
    bool reachedExperiment = false;
    for ( const LineageNode &node : ancestors.nodes )
        if ( node.id.kind == QStringLiteral( "experiment" )
             && node.id.id == QStringLiteral( "exp-src" ) )
            reachedExperiment = true;
    CHECK( reachedExperiment );
}

TEST_CASE( "legacy 3-key lineage bundles keep the named-gap warning (R24-B compat)",
           "[r24_b][experiment][repro][lineage][compat]" )
{
    Fixture fx;
    const auto exportReport = fx.exportBundle( QStringLiteral( "bundle-legacy" ) );
    REQUIRE( exportReport.ok );

    // Hand the importer a bundle whose provenance.json carries only the
    // legacy shape (no from_*/to_*), re-signed so the integrity gate passes.
    {
        QJsonObject provenance = readProvenance( exportReport.bundlePath );
        QJsonArray legacy;
        const QJsonArray edges = provenance.value( QStringLiteral( "run_edges" ) ).toArray();
        for ( const QJsonValue &value : edges )
        {
            const QJsonObject item = value.toObject();
            QJsonObject legacyItem;
            legacyItem.insert( QStringLiteral( "edge" ),
                               item.value( QStringLiteral( "edge" ) ) );
            legacyItem.insert( QStringLiteral( "other_kind" ),
                               item.value( QStringLiteral( "other_kind" ) ) );
            legacyItem.insert( QStringLiteral( "other_id" ),
                               item.value( QStringLiteral( "other_id" ) ) );
            legacy.append( legacyItem );
        }
        provenance.insert( QStringLiteral( "run_edges" ), legacy );
        writeProvenanceAndResign( exportReport.bundlePath, provenance );
    }

    ExperimentStore target;
    REQUIRE( target.open( fx.dir.filePath( QStringLiteral( "target.db" ) ) ) );
    Experiment home;
    home.setExperimentId( QStringLiteral( "exp-dst" ) );
    home.setName( QStringLiteral( "destination" ) );
    REQUIRE( target.upsertExperiment( home ).has_value() );
    ReproductionBundleImporter importer( target );
    ReproductionBundleImportOptions options;
    options.bundleDir = exportReport.bundlePath;
    options.targetExperimentId = QStringLiteral( "exp-dst" );
    const auto report = importer.importRun( options );

    REQUIRE( report.ok );
    CHECK( report.lineageEdgesReattached == 0 );
    const QString named = report.warnings.join( QLatin1Char( ';' ) );
    CHECK( named.contains( QStringLiteral( "legacy" ) ) );
    CHECK( named.contains( QStringLiteral( "cannot be re-attached" ) ) );
    // Nothing fabricated for direction-less items.
    CHECK( target.outgoingEdges( QStringLiteral( "run" ), report.runId ).isEmpty() );
    CHECK( target.incomingEdges( QStringLiteral( "run" ), report.runId ).isEmpty() );
}

TEST_CASE( "hostile lineage items are skipped with named warnings, never "
           "fabricated (R24-B)",
           "[r24_b][experiment][repro][lineage][adversarial]" )
{
    Fixture fx;
    const auto exportReport = fx.exportBundle( QStringLiteral( "bundle-hostile" ) );
    REQUIRE( exportReport.ok );

    const QString originalRunId = QStringLiteral( "run-src" );
    auto edgeItem = []( const QString &fk, const QString &fid, const QString &ek,
                        const QString &tk, const QString &tid )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "edge" ), ek );
        item.insert( QStringLiteral( "from_kind" ), fk );
        item.insert( QStringLiteral( "from_id" ), fid );
        item.insert( QStringLiteral( "to_kind" ), tk );
        item.insert( QStringLiteral( "to_id" ), tid );
        return item;
    };

    QJsonArray hostile;
    // Kind outside the vocabulary.
    hostile.append( edgeItem( QStringLiteral( "run" ), originalRunId,
                              QStringLiteral( "produced" ), QStringLiteral( "daemon" ),
                              QStringLiteral( "x" ) ) );
    // Foreign run as the far endpoint — third-party provenance fabrication.
    hostile.append( edgeItem( QStringLiteral( "run" ), originalRunId,
                              QStringLiteral( "produced" ), QStringLiteral( "run" ),
                              QStringLiteral( "run-other" ) ) );
    // Self-edge.
    hostile.append( edgeItem( QStringLiteral( "run" ), originalRunId,
                              QStringLiteral( "produced" ), QStringLiteral( "run" ),
                              originalRunId ) );
    // Neither endpoint is the recorded run.
    hostile.append( edgeItem( QStringLiteral( "experiment" ), QStringLiteral( "e" ),
                              QStringLiteral( "produced" ), QStringLiteral( "artifact" ),
                              QStringLiteral( "a" ) ) );
    // Oversize id.
    hostile.append( edgeItem( QStringLiteral( "run" ), originalRunId,
                              QStringLiteral( "produced" ), QStringLiteral( "artifact" ),
                              QString( 300, QLatin1Char( 'a' ) ) ) );
    // Dataset-side endpoint without a DatasetStore on the importer.
    hostile.append( edgeItem( QStringLiteral( "run" ), originalRunId,
                              QStringLiteral( "consumed" ),
                              QStringLiteral( "dataset_version" ),
                              QStringLiteral( "dv-1" ) ) );
    // One legitimate item amid the hostile set.
    hostile.append( edgeItem( QStringLiteral( "experiment" ), QStringLiteral( "exp-src" ),
                              QStringLiteral( "produced" ), QStringLiteral( "run" ),
                              originalRunId ) );

    {
        QJsonObject provenance;
        provenance.insert( QStringLiteral( "run_edges" ), hostile );
        writeProvenanceAndResign( exportReport.bundlePath, provenance );
    }

    ExperimentStore target;
    REQUIRE( target.open( fx.dir.filePath( QStringLiteral( "target.db" ) ) ) );
    Experiment home;
    home.setExperimentId( QStringLiteral( "exp-dst" ) );
    home.setName( QStringLiteral( "destination" ) );
    REQUIRE( target.upsertExperiment( home ).has_value() );
    ReproductionBundleImporter importer( target );
    ReproductionBundleImportOptions options;
    options.bundleDir = exportReport.bundlePath;
    options.targetExperimentId = QStringLiteral( "exp-dst" );
    const auto report = importer.importRun( options );

    // The run still imports as evidence; only the one legitimate edge was
    // re-attached; every hostile item produced a named warning.
    REQUIRE( report.ok );
    CHECK( report.lineageEdgesReattached == 1 );
    CHECK( target.incomingEdges( QStringLiteral( "run" ), report.runId ).size() == 1 );
    CHECK( target.outgoingEdges( QStringLiteral( "run" ), report.runId ).isEmpty() );

    const QString named = report.warnings.join( QLatin1Char( ';' ) );
    CHECK( named.contains( QStringLiteral( "not a well-formed lineage edge" ) ) );
    CHECK( named.contains( QStringLiteral( "exactly one endpoint" ) ) );
    CHECK( named.contains( QStringLiteral( "refuses to fabricate third-party provenance" ) ) );
    CHECK( named.contains( QStringLiteral( "dataset-side" ) ) );
}

TEST_CASE( "dataset-side lineage edges re-attach when the importer holds a "
           "DatasetStore (R24-B)",
           "[r24_b][experiment][repro][lineage][dataset]" )
{
    Fixture fx;
    // Seed a dataset-side edge around the run: run consumed dataset_version.
    REQUIRE( fx.datasets
                 .addLineageEdge( QStringLiteral( "run" ), QStringLiteral( "run-src" ),
                                  QStringLiteral( "consumed" ),
                                  QStringLiteral( "dataset_version" ),
                                  QStringLiteral( "dv-77" ) )
                 .has_value() );
    const auto exportReport = fx.exportBundle( QStringLiteral( "bundle-ds" ) );
    REQUIRE( exportReport.ok );
    {
        const QJsonObject provenance = readProvenance( exportReport.bundlePath );
        REQUIRE( provenance.value( QStringLiteral( "run_edges" ) ).toArray().size() == 3 );
    }

    // Importer WITHOUT a DatasetStore: the dataset-side edge is a named gap.
    {
        ExperimentStore target;
        REQUIRE( target.open( fx.dir.filePath( QStringLiteral( "target-a.db" ) ) ) );
        Experiment home;
        home.setExperimentId( QStringLiteral( "exp-dst-a" ) );
        home.setName( QStringLiteral( "a" ) );
        REQUIRE( target.upsertExperiment( home ).has_value() );
        ReproductionBundleImporter importer( target );
        ReproductionBundleImportOptions options;
        options.bundleDir = exportReport.bundlePath;
        options.targetExperimentId = QStringLiteral( "exp-dst-a" );
        const auto report = importer.importRun( options );
        REQUIRE( report.ok );
        CHECK( report.lineageEdgesReattached == 2 ); // experiment-side only
        CHECK( report.warnings.join( QLatin1Char( ';' ) )
                   .contains( QStringLiteral( "dataset-side" ) ) );
    }

    // Importer WITH a DatasetStore: the dataset-side edge re-attaches there,
    // under the rewritten run id.
    {
        ExperimentStore target;
        REQUIRE( target.open( fx.dir.filePath( QStringLiteral( "target-b.db" ) ) ) );
        DatasetStore targetDatasets;
        REQUIRE( targetDatasets.open(
            fx.dir.filePath( QStringLiteral( "target-datasets.db" ) ) ) );
        Experiment home;
        home.setExperimentId( QStringLiteral( "exp-dst-b" ) );
        home.setName( QStringLiteral( "b" ) );
        REQUIRE( target.upsertExperiment( home ).has_value() );
        ReproductionBundleImporter importer( target, targetDatasets );
        ReproductionBundleImportOptions options;
        options.bundleDir = exportReport.bundlePath;
        options.targetExperimentId = QStringLiteral( "exp-dst-b" );
        const auto report = importer.importRun( options );
        REQUIRE( report.ok );
        CHECK( report.lineageEdgesReattached == 3 );
        const auto consumed =
            targetDatasets.outgoingEdges( QStringLiteral( "run" ), report.runId );
        REQUIRE( consumed.size() == 1 );
        CHECK( consumed.first().edgeKind == QStringLiteral( "consumed" ) );
        CHECK( consumed.first().toKind == QStringLiteral( "dataset_version" ) );
        CHECK( consumed.first().toId == QStringLiteral( "dv-77" ) );
    }
}
