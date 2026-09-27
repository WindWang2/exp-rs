// test_dataset_version_migration.cpp — Track 13 R4 WP-E: version migration
// compatibility matrix and diff determinism.
//
// The manifest contract is strict-version, unknown-field-tolerant
// (dataset_manifest.h). This file pins:
//   - the golden v1 document (the ONLY shipped schema version): parse →
//     model → canonical text round-trips byte-identically, and its
//     fingerprint hex is a pinned constant — any canonicalizer drift or
//     silent schema change trips the tripwire;
//   - foreign versions are refused loudly with actual + expected versions;
//   - unknown fields are tolerated within a version;
//   - reads never write back (no implicit migration on the read path);
//   - diffManifests output is deterministic: entry vectors are ordered by
//     (kind, refId), not by container iteration order.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_fingerprint.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store_impl.h"
#include "dataset/dataset_version.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using namespace sicnu::dataset;

namespace
{

/// Golden v1 manifest: a complete, realistic committed document in exactly
/// the byte shape DatasetManifest::toJson produces (ids fixed, not
/// generated, so the document is stable across machines). Generated once
/// from the in-tree serializer; changing it is a deliberate migration act.
QJsonObject goldenManifestJson()
{
    QJsonObject resolution;
    resolution.insert( QStringLiteral( "x" ), 10.0 );
    resolution.insert( QStringLiteral( "y" ), 10.0 );
    resolution.insert( QStringLiteral( "unit" ), QStringLiteral( "m" ) );

    QJsonObject schema;
    schema.insert( QStringLiteral( "modality" ), QStringLiteral( "optical" ) );
    schema.insert( QStringLiteral( "sensor" ), QStringLiteral( "Sentinel-2/S2MSI2A" ) );
    schema.insert( QStringLiteral( "crs" ), QStringLiteral( "EPSG:32650" ) );
    schema.insert( QStringLiteral( "resolution" ), resolution );

    QJsonObject source;
    source.insert( QStringLiteral( "asset_id" ),
                   QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" ) );
    source.insert( QStringLiteral( "revision" ), qint64( 3 ) );
    source.insert( QStringLiteral( "role" ), QStringLiteral( "image" ) );

    QJsonObject entry;
    entry.insert( QStringLiteral( "kind" ), QStringLiteral( "asset" ) );
    entry.insert( QStringLiteral( "ref_id" ),
                  QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" ) );
    entry.insert( QStringLiteral( "revision" ), qint64( 3 ) );
    entry.insert( QStringLiteral( "role" ), QStringLiteral( "image" ) );

    QJsonObject extent;
    extent.insert( QStringLiteral( "min_x" ), 116.0 );
    extent.insert( QStringLiteral( "min_y" ), 39.0 );
    extent.insert( QStringLiteral( "max_x" ), 117.0 );
    extent.insert( QStringLiteral( "max_y" ), 40.0 );

    QJsonObject golden;
    golden.insert( QStringLiteral( "schema_version" ), kDatasetManifestSerializationVersion );
    golden.insert( QStringLiteral( "dataset_id" ),
                   QStringLiteral( "11111111-2222-4333-8444-555555555555" ) );
    golden.insert( QStringLiteral( "version_id" ),
                   QStringLiteral( "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee" ) );
    golden.insert( QStringLiteral( "name" ), QStringLiteral( "golden-v1" ) );
    golden.insert( QStringLiteral( "description" ), QStringLiteral( "pinned migration fixture" ) );
    golden.insert( QStringLiteral( "created_at_utc" ),
                   QStringLiteral( "2026-09-07T00:00:00.000Z" ) );
    golden.insert( QStringLiteral( "source_assets" ), QJsonArray{ source } );
    golden.insert( QStringLiteral( "entries" ), QJsonArray{ entry } );
    golden.insert( QStringLiteral( "schema" ), schema );
    golden.insert( QStringLiteral( "spatial_extent" ), extent );
    golden.insert( QStringLiteral( "tags" ), QJsonArray{ QStringLiteral( "golden" ), QStringLiteral( "v1" ) } );
    golden.insert( QStringLiteral( "license" ), QStringLiteral( "CC-BY-4.0" ) );
    return golden;
}

DatasetManifest goldenParsed()
{
    const auto parsed = DatasetManifest::fromJson( goldenManifestJson() );
    REQUIRE( parsed.has_value() );
    return parsed.value();
}

QString entryKey( const DatasetEntry &entry )
{
    return QStringLiteral( "%1|%2" ).arg( entry.kind, entry.refId );
}

} // namespace

TEST_CASE( "golden v1 manifest: parse → model → canonical text is byte-stable",
           "[dataset][manifest][migration][golden]" )
{
    const QJsonObject golden = goldenManifestJson();
    const DatasetManifest manifest = goldenParsed();
    CHECK( manifest.datasetId() == QStringLiteral( "11111111-2222-4333-8444-555555555555" ) );
    CHECK( manifest.versionId() == QStringLiteral( "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee" ) );
    CHECK( manifest.name() == QStringLiteral( "golden-v1" ) );
    CHECK( manifest.entries().size() == 1 );
    CHECK( manifest.sourceAssets().size() == 1 );
    CHECK( manifest.schema().modality == QStringLiteral( "optical" ) );
    CHECK( manifest.schema().resolutionX == 10.0 );
    CHECK( manifest.spatialExtent().valid );
    CHECK( manifest.spatialExtent().minimumX == 116.0 );

    // Read path never writes back implicitly: the model re-serializes to
    // exactly the golden document (unknown-field-free, canonical field set).
    CHECK( QJsonDocument( manifest.toJson() ).toJson( QJsonDocument::Compact ) ==
           QJsonDocument( golden ).toJson( QJsonDocument::Compact ) );

    // The store text funnel (Compact text → parse → Compact text) is
    // byte-identical too — persisted golden text survives a load cycle.
    const QString goldenText = jsonToText( golden );
    CHECK( jsonToText( textToJson( goldenText ) ) == goldenText );
}

TEST_CASE( "golden manifest fingerprint is a pinned constant (canonicalizer tripwire)",
           "[dataset][manifest][migration][golden]" )
{
    // Generated once from the in-tree canonicalizer over the golden v1
    // document (the fingerprint field is excluded from hashing, per
    // contract; the golden document carries none). Deliberately changing
    // this constant is how a canonicalization change gets reviewed — it
    // must never move as a side effect of an unrelated edit.
    const QString hex = makeDatasetFingerprint( goldenManifestJson() ).toHex();
    INFO( "golden fingerprint: " << hex.toStdString() );
    CHECK( hex == QStringLiteral( "a23dfda4bd54daf35842a066d3a262fc424d2483e2822a8f75cf3152ac6f7e9e" ) );
}

TEST_CASE( "foreign schema versions are refused loudly and unknown fields tolerated",
           "[dataset][manifest][migration][matrix]" )
{
    const QJsonObject golden = goldenManifestJson();

    // Future version (reader is older): refused, error names both versions.
    QJsonObject future = golden;
    future.insert( QStringLiteral( "schema_version" ), qint64( 2 ) );
    const auto refused = DatasetManifest::fromJson( future );
    REQUIRE( !refused.has_value() );
    REQUIRE( !refused.diagnostics().isEmpty() );
    CHECK( refused.diagnostics().first().code == QStringLiteral( "dataset.manifest_version" ) );
    CHECK( refused.diagnostics().first().message.contains( QStringLiteral( "2" ) ) );
    CHECK( refused.diagnostics().first().message.contains( QStringLiteral( "1" ) ) );

    // Missing version: also a version failure, never a guess.
    QJsonObject unversioned = golden;
    unversioned.remove( QStringLiteral( "schema_version" ) );
    CHECK( !DatasetManifest::fromJson( unversioned ).has_value() );

    // Unknown fields inside a known version are tolerated (forward
    // compatibility within a version) and dropped on re-serialize.
    QJsonObject extended = golden;
    extended.insert( QStringLiteral( "future_field" ), QStringLiteral( "ignored" ) );
    const auto tolerant = DatasetManifest::fromJson( extended );
    REQUIRE( tolerant.has_value() );
    CHECK( tolerant.value().toJson() == goldenParsed().toJson() );
}

TEST_CASE( "diffManifests output is ordered by (kind, refId), never by hash order",
           "[dataset][version][diff][determinism]" )
{
    DatasetManifest from = goldenParsed();
    DatasetManifest to = goldenParsed();

    // Entries that exist on BOTH sides (context, not diff material).
    auto sharedEntry = []( const QString &kind, const QString &refId ) {
        DatasetEntry entry;
        entry.kind = kind;
        entry.refId = refId;
        entry.role = QStringLiteral( "image" );
        return entry;
    };
    from.entries().append( sharedEntry( QStringLiteral( "asset" ),
                                        QStringLiteral( "zzz-shared" ) ) );
    to.entries().append( sharedEntry( QStringLiteral( "asset" ),
                                      QStringLiteral( "zzz-shared" ) ) );

    // Removed: present only in `from`.
    from.entries().append( sharedEntry( QStringLiteral( "sample" ),
                                        QStringLiteral( "mmm-removed" ) ) );

    // Changed: same identity, different role.
    DatasetEntry changed = sharedEntry( QStringLiteral( "asset" ),
                                        QStringLiteral( "aaa-changed" ) );
    from.entries().append( changed );
    DatasetEntry changedTo = changed;
    changedTo.role = QStringLiteral( "label" );
    to.entries().append( changedTo );

    // Added: several, in scrambled kind order, present only in `to`.
    to.entries().append( sharedEntry( QStringLiteral( "sample" ),
                                      QStringLiteral( "mmm-added" ) ) );
    to.entries().append( sharedEntry( QStringLiteral( "asset" ),
                                      QStringLiteral( "aaa-added" ) ) );
    to.entries().append( sharedEntry( QStringLiteral( "sample" ),
                                      QStringLiteral( "bbb-added" ) ) );
    to.entries().append( sharedEntry( QStringLiteral( "asset" ),
                                      QStringLiteral( "kkk-added" ) ) );

    const auto diff = diffManifests( from, to );
    REQUIRE( diff.has_value() );

    // Order contract: each vector is sorted by (kind, refId).
    const auto sortedBy = []( QVector<DatasetEntry> entries ) {
        std::sort( entries.begin(), entries.end(),
                   []( const DatasetEntry &a, const DatasetEntry &b ) {
                       return entryKey( a ) < entryKey( b );
                   } );
        return entries;
    };
    CHECK( diff.value().addedEntries == sortedBy( diff.value().addedEntries ) );
    CHECK( diff.value().removedEntries == sortedBy( diff.value().removedEntries ) );
    CHECK( diff.value().changedEntries == sortedBy( diff.value().changedEntries ) );
    CHECK( diff.value().addedEntries.size() == 4 );
    CHECK( diff.value().removedEntries.size() == 1 );
    CHECK( diff.value().changedEntries.size() == 1 );

    // Double-run: serializing the diff twice is byte-identical.
    const QByteArray first =
        QJsonDocument( diff.value().toJson() ).toJson( QJsonDocument::Compact );
    const auto rerun = diffManifests( from, to );
    REQUIRE( rerun.has_value() );
    const QByteArray second =
        QJsonDocument( rerun.value().toJson() ).toJson( QJsonDocument::Compact );
    CHECK( first == second );
}
