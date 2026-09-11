// test_mlops9_version_dag.cpp — Scientific MLOps 9.0 dataset version DAG
// (goal M1) + sample temporal-validity governance (goal M2).
//
// Store contract under test:
//   - a version's parent link must resolve INSIDE the same dataset at write
//     time (dangling / cross-dataset parents are typed refusals, never
//     silent DAG forks);
//   - ancestor walks are total: dangling interior links and cycles (which
//     pre-validation stores may contain) surface as typed failures;
//   - createDerivedVersion forks committed content into a fresh immutable-
//     rooted draft without touching the parent;
//   - sample labels carry an optional validity window that round-trips and
//     refuses empty windows / observations outside the window.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "dataset/dataset_version.h"
#include "dataset/sample.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <sqlite3.h>

using namespace sicnu::dataset;

namespace
{

DatasetManifest makeManifest( const QString &datasetId, const QString &versionId,
                              const QString &parentId = QString() )
{
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId );
    manifest.setVersionId( versionId );
    if ( !parentId.isEmpty() )
        manifest.setParentVersionId( parentId );
    manifest.setName( QStringLiteral( "MLOps 9.0 DAG Test Dataset" ) );
    manifest.setCreatedAtUtc( QDateTime::fromString(
        QStringLiteral( "2026-09-11T00:00:00.000Z" ), Qt::ISODateWithMs ) );

    SourceAssetRef source;
    source.assetId = QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" );
    source.revision = 1;
    source.role = QStringLiteral( "image" );
    manifest.sourceAssets().append( source );

    DatasetEntry entry;
    entry.kind = QStringLiteral( "asset" );
    entry.refId = QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" );
    entry.role = QStringLiteral( "image" );
    manifest.entries().append( entry );
    return manifest;
}

/// Commits a full chain root → v1 → v2 …; returns the version ids in
/// creation order (root first).
QVector<QString> commitChain( DatasetStore &store, const QString &datasetId, int depth )
{
    QVector<QString> ids;
    QString parent;
    for ( int level = 0; level < depth; ++level )
    {
        const QString versionId = DatasetVersionId::generate().toString();
        auto manifest = makeManifest( datasetId, versionId, parent );
        REQUIRE( store.createDraftVersion( manifest ).has_value() );
        REQUIRE( store.stageVersion( DatasetVersionId::fromString( versionId ).value() ).has_value() );
        REQUIRE( store.commitVersion( DatasetVersionId::fromString( versionId ).value() ).has_value() );
        ids.append( versionId );
        parent = versionId;
    }
    return ids;
}

SampleRecord pointSample( const QString &versionId )
{
    SampleRecord sample;
    sample.setSampleId( SampleId::generate().toString() );
    sample.setDatasetVersionId( versionId );
    sample.setKind( SampleKind::Point );
    PointSample point;
    point.x = 1.0;
    point.y = 2.0;
    sample.payload() = point;
    return sample;
}

} // namespace

TEST_CASE( "createDraftVersion refuses dangling and cross-dataset parents",
           "[mlops9][dag]" )
{
    DatasetStore store;
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const QString path = tempDir.filePath( QStringLiteral( "mlops9_dag_parent.sqlite" ) );
    QFile::remove( path );
    REQUIRE( store.open( path ) );

    const QString datasetId = DatasetId::generate().toString();
    REQUIRE( store.createDataset( DatasetId::fromString( datasetId ).value(), QStringLiteral( "d1" ) ).has_value() );

    // Dangling parent: the id text is well-formed but no such version exists.
    const QString dangling = DatasetVersionId::generate().toString();
    auto orphan = store.createDraftVersion(
        makeManifest( datasetId, DatasetVersionId::generate().toString(), dangling ) );
    REQUIRE( !orphan.has_value() );
    REQUIRE( orphan.diagnostics().first().code == QStringLiteral( "dataset.parent_not_found" ) );

    // Cross-dataset parent: exists, but belongs to another dataset.
    const QString otherDatasetId = DatasetId::generate().toString();
    REQUIRE( store
                 .createDataset( DatasetId::fromString( otherDatasetId ).value(),
                                 QStringLiteral( "d2" ) )
                 .has_value() );
    const QString foreignVersion = DatasetVersionId::generate().toString();
    REQUIRE( store
                 .createDraftVersion(
                     makeManifest( otherDatasetId, foreignVersion ) )
                 .has_value() );
    auto mismatch = store.createDraftVersion( makeManifest(
        datasetId, DatasetVersionId::generate().toString(), foreignVersion ) );
    REQUIRE( !mismatch.has_value() );
    REQUIRE( mismatch.diagnostics().first().code ==
             QStringLiteral( "dataset.parent_dataset_mismatch" ) );

    // A valid chain still works.
    const auto chain = commitChain( store, datasetId, 2 );
    REQUIRE( chain.size() == 2 );
    store.close();
}

TEST_CASE( "versionAncestors walks child to root and refuses corrupt lineages",
           "[mlops9][dag]" )
{
    DatasetStore store;
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const QString path = tempDir.filePath( QStringLiteral( "mlops9_dag_ancestors.sqlite" ) );
    QFile::remove( path );
    REQUIRE( store.open( path ) );

    const QString datasetId = DatasetId::generate().toString();
    REQUIRE( store.createDataset( DatasetId::fromString( datasetId ).value(), QStringLiteral( "d" ) ).has_value() );
    const auto chain = commitChain( store, datasetId, 3 ); // root → v1 → v2

    // Full walk: child first, root last.
    const auto ancestors =
        store.versionAncestors( DatasetVersionId::fromString( chain.last() ).value() );
    REQUIRE( ancestors.has_value() );
    REQUIRE( ancestors.value().size() == 3 );
    REQUIRE( ancestors.value().first().versionId() == chain.last() );
    REQUIRE( ancestors.value().last().versionId() == chain.first() );

    // Missing start version.
    const auto missing = store.versionAncestors( DatasetVersionId::generate() );
    REQUIRE( !missing.has_value() );
    REQUIRE( missing.diagnostics().first().code == QStringLiteral( "dataset.parent_not_found" ) );

    store.close();

    // Corrupt a lineage link directly (a store written before parent
    // validation could contain this): point v2's parent at a missing id.
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( path.toUtf8().constData(), &raw, SQLITE_OPEN_READWRITE,
                                  nullptr ) == SQLITE_OK );
        char *error = nullptr;
        REQUIRE( sqlite3_exec( raw, "UPDATE dataset_versions SET parent_version_id='dead'"
                                    " WHERE parent_version_id IS NOT NULL AND"
                                    " id != (SELECT MIN(id) FROM dataset_versions);",
                               nullptr, nullptr, &error ) == SQLITE_OK );
        sqlite3_free( error );
        sqlite3_close( raw );
    }

    REQUIRE( store.open( path ) );
    const auto dangling = store.versionAncestors(
        DatasetVersionId::fromString( chain.last() ).value() );
    REQUIRE( !dangling.has_value() );
    REQUIRE( dangling.diagnostics().first().code == QStringLiteral( "dataset.parent_not_found" ) );
    REQUIRE( dangling.diagnostics().first().message.contains( QStringLiteral( "dangling" ) ) );

    // Cycle: v1 → root → v1. Injection via SQL because the write path now
    // refuses to CREATE cycles — the read path must still detect legacy ones.
    store.close();
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( path.toUtf8().constData(), &raw, SQLITE_OPEN_READWRITE,
                                  nullptr ) == SQLITE_OK );
        const std::string clear =
            "UPDATE dataset_versions SET parent_version_id='';";
        const std::string a =
            "UPDATE dataset_versions SET parent_version_id='" + chain.at( 1 ).toStdString() +
            "' WHERE id='" + chain.first().toStdString() + "';";
        const std::string b =
            "UPDATE dataset_versions SET parent_version_id='" + chain.first().toStdString() +
            "' WHERE id='" + chain.at( 1 ).toStdString() + "';";
        char *error = nullptr;
        REQUIRE( sqlite3_exec( raw, clear.c_str(), nullptr, nullptr, &error ) == SQLITE_OK );
        REQUIRE( sqlite3_exec( raw, a.c_str(), nullptr, nullptr, &error ) == SQLITE_OK );
        REQUIRE( sqlite3_exec( raw, b.c_str(), nullptr, nullptr, &error ) == SQLITE_OK );
        sqlite3_free( error );
        sqlite3_close( raw );
    }
    REQUIRE( store.open( path ) );
    const auto cycle = store.versionAncestors( DatasetVersionId::fromString( chain.at( 1 ) ).value() );
    REQUIRE( !cycle.has_value() );
    REQUIRE( cycle.diagnostics().first().code == QStringLiteral( "dataset.version_cycle" ) );

    store.close();
}

TEST_CASE( "versionChildren inverts the parent link", "[mlops9][dag]" )
{
    DatasetStore store;
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const QString path = tempDir.filePath( QStringLiteral( "mlops9_dag_children.sqlite" ) );
    QFile::remove( path );
    REQUIRE( store.open( path ) );
    const QString datasetId = DatasetId::generate().toString();
    REQUIRE( store.createDataset( DatasetId::fromString( datasetId ).value(), QStringLiteral( "d" ) ).has_value() );
    const auto chain = commitChain( store, datasetId, 3 );

    const auto rootChildren =
        store.versionChildren( DatasetVersionId::fromString( chain.first() ).value() );
    REQUIRE( rootChildren.size() == 1 );
    REQUIRE( rootChildren.first().versionId() == chain.at( 1 ) );

    // Leaves have no children.
    REQUIRE( store
                 .versionChildren(
                     DatasetVersionId::fromString( chain.last() ).value() )
                 .isEmpty() );
    store.close();
}

TEST_CASE( "createDerivedVersion forks committed content into a fresh draft",
           "[mlops9][dag]" )
{
    DatasetStore store;
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const QString path = tempDir.filePath( QStringLiteral( "mlops9_dag_derive.sqlite" ) );
    QFile::remove( path );
    REQUIRE( store.open( path ) );
    const QString datasetId = DatasetId::generate().toString();
    REQUIRE( store.createDataset( DatasetId::fromString( datasetId ).value(), QStringLiteral( "d" ) ).has_value() );
    const auto chain = commitChain( store, datasetId, 1 );
    const auto rootId = DatasetVersionId::fromString( chain.first() ).value();

    // Deriving from a committed parent works and copies content.
    const auto derived = store.createDerivedVersion( rootId, QStringLiteral( "fork for QA" ) );
    REQUIRE( derived.has_value() );
    REQUIRE( derived.value().status() == DatasetVersionStatus::Draft );
    REQUIRE( derived.value().datasetId() == datasetId );
    REQUIRE( derived.value().parentVersionId() == chain.first() );
    REQUIRE( derived.value().versionId() != chain.first() );
    // The copied manifest keeps entries but carries the new identity.
    const auto parsed = DatasetManifest::fromJson(
        QJsonDocument::fromJson( derived.value().manifestJson().toUtf8() ).object() );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed.value().entries().size() == 1 );
    REQUIRE( parsed.value().parentVersionId() == chain.first() );
    REQUIRE( parsed.value().fingerprint().isEmpty() );

    // The derived draft can stage + commit into a second committed node.
    REQUIRE( store.stageVersion( DatasetVersionId::fromString( derived.value().versionId() ).value() )
                 .has_value() );
    const auto committed =
        store.commitVersion( DatasetVersionId::fromString( derived.value().versionId() ).value() );
    REQUIRE( committed.has_value() );
    REQUIRE( !committed.value().fingerprint().isEmpty() );

    // Lineage: derived → root.
    const auto lineage =
        store.versionAncestors( DatasetVersionId::fromString( derived.value().versionId() ).value() );
    REQUIRE( lineage.has_value() );
    REQUIRE( lineage.value().size() == 2 );

    // Deriving from a DRAFT is refused: drafts are mutable targets.
    const auto draft = store.createDraftVersion(
        makeManifest( datasetId, DatasetVersionId::generate().toString() ) );
    REQUIRE( draft.has_value() );
    const auto refuse =
        store.createDerivedVersion( DatasetVersionId::fromString( draft.value().versionId() ).value() );
    REQUIRE( !refuse.has_value() );
    REQUIRE( refuse.diagnostics().first().code ==
             QStringLiteral( "dataset.derive_requires_committed" ) );

    // Missing parent.
    const auto missing = store.createDerivedVersion( DatasetVersionId::generate() );
    REQUIRE( !missing.has_value() );
    REQUIRE( missing.diagnostics().first().code == QStringLiteral( "dataset.not_found" ) );

    store.close();
}

TEST_CASE( "sample validity windows round-trip and validate (M2)",
           "[mlops9][governance]" )
{
    const QString versionId = DatasetVersionId::generate().toString();
    SampleRecord sample = pointSample( versionId );
    const QDateTime from = QDateTime::fromString( QStringLiteral( "2020-01-01T00:00:00.000Z" ),
                                                  Qt::ISODateWithMs );
    const QDateTime until = QDateTime::fromString( QStringLiteral( "2020-12-31T23:59:59.000Z" ),
                                                   Qt::ISODateWithMs );
    sample.setValidFromUtc( from );
    sample.setValidUntilUtc( until );
    sample.setTimeUtc( QDateTime::fromString( QStringLiteral( "2020-06-15T10:00:00.000Z" ),
                                              Qt::ISODateWithMs ) );
    REQUIRE( validateSample( sample ).has_value() );

    // Round-trip through JSON.
    const auto restored = SampleRecord::fromJson( sample.toJson() );
    REQUIRE( restored.has_value() );
    REQUIRE( restored.value().validFromUtc() == from );
    REQUIRE( restored.value().validUntilUtc() == until );

    // Old-style samples (no window) keep validating — backward compatible.
    SampleRecord legacy = pointSample( versionId );
    REQUIRE( validateSample( legacy ).has_value() );
    REQUIRE( !legacy.validFromUtc().isValid() );

    // Empty window (from > until) refused.
    SampleRecord inverted = pointSample( versionId );
    inverted.setValidFromUtc( until );
    inverted.setValidUntilUtc( from );
    REQUIRE( !validateSample( inverted ).has_value() );

    // Observation outside the window refused.
    SampleRecord outside = sample;
    outside.setTimeUtc( QDateTime::fromString( QStringLiteral( "2021-06-15T10:00:00.000Z" ),
                                               Qt::ISODateWithMs ) );
    const auto refused = validateSample( outside );
    REQUIRE( !refused.has_value() );
    REQUIRE( refused.diagnostics().first().message.contains( QStringLiteral( "exceeds" ) ) );

    // Store round-trip: validity survives persistence.
    DatasetStore store;
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const QString path = tempDir.filePath( QStringLiteral( "mlops9_sample_validity.sqlite" ) );
    QFile::remove( path );
    REQUIRE( store.open( path ) );
    const QString datasetId = DatasetId::generate().toString();
    REQUIRE( store.createDataset( DatasetId::fromString( datasetId ).value(), QStringLiteral( "d" ) ).has_value() );
    REQUIRE( store.createDraftVersion( makeManifest( datasetId, versionId ) ).has_value() );
    REQUIRE( store.addSamples( { sample } ).has_value() );
    const auto loaded = store.sampleById(
        DatasetVersionId::fromString( versionId ).value(),
        SampleId::fromString( sample.sampleId() ).value() );
    REQUIRE( loaded.has_value() );
    REQUIRE( loaded.value().validFromUtc() == from );
    REQUIRE( loaded.value().validUntilUtc() == until );
    store.close();
}
