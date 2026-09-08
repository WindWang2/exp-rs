// test_dataset_core.cpp — Foundation 5.0 dataset core tests (ADR 0134):
// strong ids, vocabulary round-trips, manifest canonical serialization,
// fingerprint stability + self-verification, version lifecycle (draft →
// stage → commit → deprecate), immutability after commit, diff, store
// forward-compat guard, lineage edges, and corruption handling.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_fingerprint.h"
#include "dataset/dataset_ids.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/dataset_version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <sqlite3.h>

#include <type_traits>

using namespace sicnu::dataset;

namespace
{

DatasetManifest makeManifest( const QString &datasetId, const QString &versionId,
                              const QString &entryRef = QStringLiteral( "asset-a" ) )
{
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId );
    manifest.setVersionId( versionId );
    manifest.setName( QStringLiteral( "Test Optical Dataset" ) );
    manifest.setCreatedAtUtc( QDateTime::fromString(
        QStringLiteral( "2026-09-07T00:00:00.000Z" ), Qt::ISODateWithMs ) );

    SourceAssetRef source;
    source.assetId = QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" );
    source.revision = 3;
    source.role = QStringLiteral( "image" );
    manifest.sourceAssets().append( source );

    DatasetEntry entry;
    entry.kind = QStringLiteral( "asset" );
    entry.refId = entryRef;
    entry.role = QStringLiteral( "image" );
    manifest.entries().append( entry );

    manifest.schema().modality = QStringLiteral( "optical" );
    manifest.schema().crs = QStringLiteral( "EPSG:32650" );
    manifest.schema().resolutionX = 10.0;
    manifest.schema().resolutionY = 10.0;
    manifest.schema().resolutionUnit = QStringLiteral( "m" );
    manifest.spatialExtent().valid = true;
    manifest.spatialExtent().minimumX = 500000.0;
    manifest.spatialExtent().minimumY = 4000000.0;
    manifest.spatialExtent().maximumX = 510000.0;
    manifest.spatialExtent().maximumY = 4010000.0;
    return manifest;
}

} // namespace

TEST_CASE( "dataset ids round-trip strictly", "[dataset][core]" )
{
    const DatasetId generated = DatasetId::generate();
    REQUIRE( !generated.isNull() );
    const auto parsed = DatasetId::fromString( generated.toString() );
    REQUIRE( parsed.has_value() );
    CHECK( *parsed == generated );

    // Malformed text is a parse failure, never a silent null id.
    CHECK( DatasetId::fromString( QStringLiteral( "not-a-uuid" ) ) == std::nullopt );
    CHECK( DatasetId::fromString( QString() ) == std::nullopt );

    // Case/brace differences normalize to one canonical identity.
    const auto braced =
        DatasetId::fromString( QStringLiteral( "{%1}" ).arg( generated.toString().toUpper() ) );
    REQUIRE( braced.has_value() );
    CHECK( *braced == generated );

    // Each id type is a distinct type: strong identity, no cross-assignment.
    static_assert( !std::is_same_v<DatasetId, DatasetVersionId> );
    const auto versionId = DatasetVersionId::generate();
    REQUIRE( versionId != DatasetVersionId{} );
}

TEST_CASE( "dataset vocabularies round-trip", "[dataset][core]" )
{
    CHECK( sampleKindFromString( sampleKindToString( SampleKind::Patch ) ) == SampleKind::Patch );
    CHECK( splitMethodFromString( splitMethodToString( SplitMethod::SpatialBlock ) ) ==
           SplitMethod::SpatialBlock );
    CHECK( leakageKindFromString( leakageKindToString( LeakageKind::PrePostPairLeakage ) ) ==
           LeakageKind::PrePostPairLeakage );
    // Unknown vocabulary is a parse failure, not a default.
    CHECK( sampleKindFromString( QStringLiteral( "voxel" ) ) == std::nullopt );
    CHECK( datasetVersionStatusFromString( QStringLiteral( "frozen" ) ) == std::nullopt );
}

TEST_CASE( "manifest serializes canonically and validates strictly", "[dataset][manifest]" )
{
    const DatasetId datasetId = DatasetId::generate();
    const DatasetVersionId versionId = DatasetVersionId::generate();
    DatasetManifest manifest = makeManifest( datasetId.toString(), versionId.toString() );

    const QJsonObject json = manifest.toJson();
    CHECK( json.value( QStringLiteral( "schema_version" ) ).toInteger() ==
           kDatasetManifestSerializationVersion );

    const auto parsed = DatasetManifest::fromJson( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed.value() == manifest );

    // Foreign schema version is rejected loudly, not coerced.
    QJsonObject foreign = json;
    foreign.insert( QStringLiteral( "schema_version" ), 999 );
    const auto rejected = DatasetManifest::fromJson( foreign );
    CHECK( !rejected.has_value() );
    CHECK( rejected.diagnostics().first().code == QStringLiteral( "dataset.manifest_version" ) );

    // Missing identity fails validation.
    QJsonObject anonymous = json;
    anonymous.remove( QStringLiteral( "dataset_id" ) );
    CHECK( !DatasetManifest::fromJson( anonymous ).has_value() );

    // Unknown fields are tolerated (forward compatibility within a version).
    QJsonObject extended = json;
    extended.insert( QStringLiteral( "future_field" ), QStringLiteral( "ignored" ) );
    const auto tolerant = DatasetManifest::fromJson( extended );
    REQUIRE( tolerant.has_value() );
    CHECK( tolerant.value() == manifest );
}

TEST_CASE( "fingerprint is canonical-content identity", "[dataset][fingerprint]" )
{
    const DatasetId datasetId = DatasetId::generate();
    const DatasetVersionId versionId = DatasetVersionId::generate();
    DatasetManifest manifest = makeManifest( datasetId.toString(), versionId.toString() );
    const QJsonObject json = manifest.toJson();

    const DatasetFingerprint fingerprint = makeDatasetFingerprint( json );
    REQUIRE( fingerprint.isValid() );
    CHECK( fingerprint.toHex().size() == 64 );

    // Same content → same fingerprint, regardless of key insertion order.
    QJsonObject reordered = json;
    const QStringList keys = json.keys();
    QJsonObject rebuilt;
    for ( auto it = keys.rbegin(); it != keys.rend(); ++it )
        rebuilt.insert( *it, json.value( *it ) );
    reordered = rebuilt;
    CHECK( makeDatasetFingerprint( reordered ).toHex() == fingerprint.toHex() );

    // Any content change → different fingerprint.
    QJsonObject changed = json;
    changed.insert( QStringLiteral( "name" ), QStringLiteral( "Renamed" ) );
    CHECK( makeDatasetFingerprint( changed ).toHex() != fingerprint.toHex() );

    // The recorded fingerprint field does not perturb the hash and can be
    // self-verified.
    QJsonObject stamped = json;
    stamped.insert( QStringLiteral( "fingerprint" ), fingerprint.toHex() );
    CHECK( makeDatasetFingerprint( stamped ).toHex() == fingerprint.toHex() );
    CHECK( manifestFingerprintMatches( stamped ) );
    CHECK( !manifestFingerprintMatches( json ) );
}

TEST_CASE( "version lifecycle: draft → stage → commit is atomic and immutable",
           "[dataset][store][version]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    REQUIRE( store.schemaVersion() == QLatin1String( kDatasetStoreSchemaVersion ) );

    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "landcover" ) ).has_value() );

    // Duplicate dataset creation is a conflict, not a silent reuse.
    const auto conflict = store.createDataset( datasetId, QStringLiteral( "again" ) );
    CHECK( !conflict.has_value() );
    CHECK( conflict.diagnostics().first().code == QStringLiteral( "dataset.conflict" ) );

    const DatasetVersionId versionId = DatasetVersionId::generate();
    const auto draft = store.createDraftVersion(
        makeManifest( datasetId.toString(), versionId.toString() ),
        QStringLiteral( "initial import" ) );
    REQUIRE( draft.has_value() );
    CHECK( draft.value().status() == DatasetVersionStatus::Draft );
    CHECK( draft.value().isMutable() );

    // Committing an unstaged-but-draft version is legal only after the
    // manifest parses; our draft manifest is valid, but the CONTRACT under
    // test here is stage→commit.
    const auto staged = store.stageVersion( versionId );
    REQUIRE( staged.has_value() );

    const auto committed = store.commitVersion( versionId );
    REQUIRE( committed.has_value() );
    CHECK( committed.value().status() == DatasetVersionStatus::Committed );
    CHECK( !committed.value().fingerprint().isEmpty() );
    CHECK( committed.value().manifestJson().contains( QStringLiteral( "fingerprint" ) ) );

    // Committed manifest self-verifies against its own fingerprint.
    const QJsonObject committedJson =
        QJsonDocument::fromJson( committed.value().manifestJson().toUtf8() ).object();
    REQUIRE( manifestFingerprintMatches( committedJson ) );

    // Immutability: staging and committing a committed version both fail.
    const auto restage = store.stageVersion( versionId );
    CHECK( !restage.has_value() );
    CHECK( restage.diagnostics().first().code == QStringLiteral( "dataset.not_draft" ) );
    const auto recommit = store.commitVersion( versionId );
    CHECK( !recommit.has_value() );

    // Latest committed lookup + version list.
    const auto latest = store.latestCommittedVersion( datasetId );
    REQUIRE( latest.has_value() );
    CHECK( latest->versionId() == committed.value().versionId() );
    CHECK( store.versionsOfDataset( datasetId ).size() == 1 );

    // Deprecation marks, never deletes.
    const auto deprecated = store.deprecateVersion( versionId );
    REQUIRE( deprecated.has_value() );
    CHECK( deprecated.value().status() == DatasetVersionStatus::Deprecated );
    const auto stillThere = store.versionById( versionId );
    REQUIRE( stillThere.has_value() );

    // Drafts are not deprecable.
    const auto draft2 = DatasetVersionId::generate();
    REQUIRE( store
                 .createDraftVersion(
                     makeManifest( datasetId.toString(), draft2.toString() ) )
                 .has_value() );
    const auto refuse = store.deprecateVersion( draft2 );
    CHECK( !refuse.has_value() );
    CHECK( refuse.diagnostics().first().code == QStringLiteral( "dataset.not_committed" ) );
}

TEST_CASE( "committed versions are immutable but child drafts carry lineage",
           "[dataset][store][version]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );

    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "pairs" ) ).has_value() );

    const auto v1Id = DatasetVersionId::generate();
    REQUIRE( store.createDraftVersion( makeManifest( datasetId.toString(), v1Id.toString() ) ).has_value() );
    REQUIRE( store.stageVersion( v1Id ).has_value() );
    REQUIRE( store.commitVersion( v1Id ).has_value() );

    // Edit = new draft child version with parent link (never in-place).
    DatasetManifest v2 = makeManifest( datasetId.toString(), DatasetVersionId::generate().toString() );
    v2.setParentVersionId( v1Id.toString() );
    v2.entries().first().refId = QStringLiteral( "asset-b" );
    const auto v2Draft = store.createDraftVersion( v2, QStringLiteral( "relabel ROI 7" ) );
    REQUIRE( v2Draft.has_value() );
    CHECK( v2Draft.value().parentVersionId() == v1Id.toString() );

    // Diff v1 → v2 shows the changed entry, not a wholesale rewrite.
    const auto v1Manifest =
        DatasetManifest::fromJson(
            QJsonDocument::fromJson( store.versionById( v1Id )->manifestJson().toUtf8() ).object() );
    REQUIRE( v1Manifest.has_value() );
    const auto diff = diffManifests( v1Manifest.value(), v2 );
    REQUIRE( diff.has_value() );
    CHECK( diff.value().addedEntries.size() == 1 );
    CHECK( diff.value().removedEntries.size() == 1 );
    CHECK( !diff.value().isEmpty() );

    // Cross-dataset diffs are refused.
    DatasetManifest other = makeManifest( DatasetId::generate().toString(),
                                          DatasetVersionId::generate().toString() );
    const auto mismatch = diffManifests( v1Manifest.value(), other );
    CHECK( !mismatch.has_value() );
    CHECK( mismatch.diagnostics().first().code == QStringLiteral( "dataset.diff_dataset_mismatch" ) );

    // Deleting a dataset with committed versions is refused; deprecate instead.
    const auto refused = store.deleteDataset( datasetId );
    CHECK( !refused.has_value() );
    CHECK( refused.diagnostics().first().code == QStringLiteral( "dataset.delete_refused" ) );
}

TEST_CASE( "deleteDataset commits its transaction; the store stays writable",
           "[dataset][store][delete]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );

    // Draft-only dataset: the deletable case.
    const DatasetId draftId = DatasetId::generate();
    REQUIRE( store.createDataset( draftId, QStringLiteral( "temp" ) ).has_value() );
    REQUIRE( store
                 .createDraftVersion(
                     makeManifest( draftId.toString(), DatasetVersionId::generate().toString() ) )
                 .has_value() );

    REQUIRE( store.deleteDataset( draftId ).has_value() );

    // #774 regression: the deletion must have COMMITTED. With the write
    // transaction left open (the baseline leaked the SQLite write lock),
    // the next store write failed to BEGIN its own transaction.
    const DatasetId nextId = DatasetId::generate();
    const auto next = store.createDataset( nextId, QStringLiteral( "after-delete" ) );
    REQUIRE( next.has_value() );

    // The deleted dataset is really gone.
    CHECK( store.versionsOfDataset( draftId ).isEmpty() );
}

TEST_CASE( "store refuses writes on a newer schema (forward tolerance)",
           "[dataset][store][migration]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "future.db" ) );
    {
        DatasetStore store;
        REQUIRE( store.open( dbPath ) );
    }
    // Simulate a future schema stamp directly.
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw,
                                  SQLITE_OPEN_READWRITE, nullptr ) == SQLITE_OK );
        sqlite3_exec( raw,
                      "INSERT OR REPLACE INTO ds_meta(key,value) VALUES('schema_version','999')",
                      nullptr, nullptr, nullptr );
        sqlite3_close( raw );
    }
    DatasetStore store;
    REQUIRE( store.open( dbPath ) );
    CHECK( store.isReadOnly() );
    const auto refused = store.createDataset( DatasetId::generate(), QStringLiteral( "x" ) );
    CHECK( !refused.has_value() );
    CHECK( refused.diagnostics().first().code == QStringLiteral( "dataset.store_read_only" ) );
}

TEST_CASE( "lineage edges are queryable in both directions", "[dataset][store][lineage]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );

    const QString datasetId = DatasetId::generate().toString();
    const QString versionId = DatasetVersionId::generate().toString();
    REQUIRE( store.addLineageEdge( QStringLiteral( "asset" ),
                                   QStringLiteral( "asset-1" ),
                                   QStringLiteral( "derived_from" ),
                                   QStringLiteral( "dataset_version" ), versionId )
                 .has_value() );
    REQUIRE( store.addLineageEdge( QStringLiteral( "dataset_version" ), versionId,
                                   QStringLiteral( "snapshot_of" ),
                                   QStringLiteral( "dataset" ), datasetId )
                 .has_value() );

    CHECK( store.outgoingEdges( QStringLiteral( "asset" ), QStringLiteral( "asset-1" ) ).size() == 1 );
    CHECK( store.incomingEdges( QStringLiteral( "dataset" ), datasetId ).size() == 1 );
    CHECK( store.incomingEdges( QStringLiteral( "dataset_version" ), versionId ).size() == 1 );
    CHECK( store.lineageEdgeCount() == 2 );

    // Duplicate edges are idempotent.
    REQUIRE( store.addLineageEdge( QStringLiteral( "asset" ),
                                   QStringLiteral( "asset-1" ),
                                   QStringLiteral( "derived_from" ),
                                   QStringLiteral( "dataset_version" ), versionId )
                 .has_value() );
    CHECK( store.lineageEdgeCount() == 2 );

    // Empty endpoints are rejected.
    const auto invalid =
        store.addLineageEdge( QString(), QStringLiteral( "x" ), QStringLiteral( "e" ),
                              QStringLiteral( "dataset" ), datasetId );
    CHECK( !invalid.has_value() );
}

TEST_CASE( "interrupted staging is reported by staleStagedDrafts", "[dataset][store][recovery]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );

    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "crashy" ) ).has_value() );

    const auto stagedId = DatasetVersionId::generate();
    REQUIRE( store.createDraftVersion( makeManifest( datasetId.toString(), stagedId.toString() ) ).has_value() );
    REQUIRE( store.stageVersion( stagedId ).has_value() );

    const auto draftId = DatasetVersionId::generate();
    REQUIRE( store.createDraftVersion( makeManifest( datasetId.toString(), draftId.toString() ) ).has_value() );

    const auto stale = store.staleStagedDrafts();
    REQUIRE( stale.size() == 1 );
    CHECK( stale.first().versionId() == stagedId.toString() );
    CHECK( stale.first().status() == DatasetVersionStatus::Draft );
}

TEST_CASE( "corrupt rows surface as absent/typed failures, never fake data",
           "[dataset][store][corruption]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "corrupt.db" ) );
    QString versionId;
    QString datasetId;
    {
        DatasetStore store;
        REQUIRE( store.open( dbPath ) );
        const DatasetId id = DatasetId::generate();
        REQUIRE( store.createDataset( id, QStringLiteral( "corrupt" ) ).has_value() );
        datasetId = id.toString();
        const auto draft = store.createDraftVersion(
            makeManifest( datasetId, DatasetVersionId::generate().toString() ) );
        REQUIRE( draft.has_value() );
        versionId = draft.value().versionId();
    }
    // Direct DB surgery: corrupt the status vocabulary of one version row.
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw,
                                  SQLITE_OPEN_READWRITE, nullptr ) == SQLITE_OK );
        const std::string corruptSql =
            "UPDATE dataset_versions SET status='bogus' WHERE id='" +
            versionId.toStdString() + "'";
        sqlite3_exec( raw, corruptSql.c_str(), nullptr, nullptr, nullptr );
        sqlite3_close( raw );
    }
    DatasetStore store;
    REQUIRE( store.open( dbPath ) );
    // A corrupt version is NOT rewritten into a fake draft: it surfaces as
    // absent for lookups, missing from listings.
    const auto looked = store.versionById(
        DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) );
    CHECK( !looked.has_value() );
    CHECK( store.versionCount() == 1 ); // the row is still there (honest count)
    // The listing skips the unreadable row instead of fabricating a draft.
    CHECK( store.versionsOfDataset(
               DatasetId::fromString( datasetId ).value_or( DatasetId{} ) )
               .isEmpty() );
}

TEST_CASE( "paged dataset listing never exceeds the page budget", "[dataset][store][catalog]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );

    constexpr int kCount = 25;
    for ( int i = kCount; i >= 1; --i )
    {
        INFO( i );
        const auto created = store.createDataset(
            DatasetId::generate(), QStringLiteral( "ds-%1" ).arg( i, 3, 10, QLatin1Char( '0' ) ) );
        REQUIRE( created.has_value() );
    }
    CHECK( store.datasetCount() == kCount );

    qint64 seen = 0;
    qint64 offset = 0;
    while ( true )
    {
        const auto page = store.listDatasets( offset, 10 );
        REQUIRE( page.has_value() );
        CHECK( page.value().first == kCount );
        CHECK( page.value().second.size() <= 10 );
        seen += page.value().second.size();
        if ( page.value().second.isEmpty() )
            break;
        offset += page.value().second.size();
        if ( offset > kCount * 2 )
            FAIL( "pagination did not terminate" );
    }
    CHECK( seen == kCount );
    // Ordered by name: first page starts at ds-001.
    const auto first = store.listDatasets( 0, 1 );
    REQUIRE( first.has_value() );
    CHECK( first.value().second.first().value( QStringLiteral( "name" ) ).toString() ==
           QStringLiteral( "ds-001" ) );
}
