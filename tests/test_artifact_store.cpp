// test_artifact_store.cpp — Phase D artifact identity store tests: schema
// lifecycle, version semantics, lookups, reference pinning, GC reap rules
// (invariant I7: active artifacts are never reapable), digest helper, and
// forward-compat schema guard.
#include <catch2/catch_test_macros.hpp>

#include "data/artifact_store.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <QCryptographicHash>

#include <sqlite3.h>

#include <cstring>

using namespace sicnu::data;

namespace
{
ArtifactRegistration makeRegistration( const QString &key, const QString &payloadPath,
                                       const QString &kind = QStringLiteral( "raster" ) )
{
    ArtifactRegistration reg;
    reg.logicalKey = key;
    reg.storagePath = payloadPath;
    reg.kind = kind;
    reg.producerFingerprint = QStringLiteral( "fp-%1" ).arg( key );
    reg.metadata = QJsonObject{ { QStringLiteral( "band" ), 1 } };
    return reg;
}

void writePayload( const QString &path, const QByteArray &bytes )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( bytes );
    f.close();
}
} // namespace

TEST_CASE( "ArtifactStore opens, persists across reopen, reports schema", "[artifact_store]" )
{
    QTemporaryDir dir;
    const QString db = dir.filePath( "artifacts.sqlite" );
    {
        ArtifactStore store;
        QString err;
        REQUIRE( store.open( db, &err ) );
        REQUIRE( store.isOpen() );
        REQUIRE( store.schemaVersion() == QStringLiteral( "1" ) );
        REQUIRE( store.count() == 0 );
    }
    ArtifactStore reopened;
    QString err;
    REQUIRE( reopened.open( db, &err ) );
    REQUIRE( reopened.count() == 0 );
    REQUIRE( reopened.schemaVersion() == QStringLiteral( "1" ) );
}

TEST_CASE( "ArtifactStore registers versions with stable identity", "[artifact_store]" )
{
    QTemporaryDir dir;
    const QString payloadA = dir.filePath( "a.tif" );
    const QString payloadB = dir.filePath( "b.tif" );
    writePayload( payloadA, "version-one-bytes" );
    writePayload( payloadB, "version-two-bytes" );

    ArtifactStore store;
    QString err;
    REQUIRE( store.open( dir.filePath( "artifacts.sqlite" ), &err ) );

    const auto first = store.registerArtifact( makeRegistration( QStringLiteral( "ndvi" ), payloadA ) );
    REQUIRE( first );
    REQUIRE( first.value().version == 1 );
    REQUIRE( first.value().state == QStringLiteral( "live" ) );
    REQUIRE( first.value().sizeBytes == static_cast<qint64>( strlen( "version-one-bytes" ) ) );
    REQUIRE( first.value().metadata.value( "band" ).toInt() == 1 );

    // Re-registering the same logical key bumps the version, keeps identity.
    const auto second = store.registerArtifact( makeRegistration( QStringLiteral( "ndvi" ), payloadB ) );
    REQUIRE( second );
    REQUIRE( second.value().version == 2 );
    REQUIRE( second.value().artifactId == first.value().artifactId );

    // Different logical key: fresh identity.
    const auto other = store.registerArtifact( makeRegistration( QStringLiteral( "mask" ), payloadA ) );
    REQUIRE( other );
    REQUIRE( other.value().version == 1 );
    REQUIRE( other.value().artifactId != first.value().artifactId );

    const auto latest = store.latestByLogicalKey( QStringLiteral( "ndvi" ) );
    REQUIRE( latest );
    REQUIRE( latest->version == 2 );
    REQUIRE( latest->storagePath == payloadB );
    REQUIRE( store.versionsByLogicalKey( QStringLiteral( "ndvi" ) ).size() == 2 );

    // Lookups by path and fingerprint.
    const auto byPath = store.latestByPath( payloadA );
    REQUIRE( byPath );
    REQUIRE( byPath->logicalKey == QStringLiteral( "mask" ) );
    REQUIRE( store.byProducerFingerprint( QStringLiteral( "fp-ndvi" ) ).size() == 2 );
}

TEST_CASE( "ArtifactStore refuses registrations without a payload", "[artifact_store]" )
{
    QTemporaryDir dir;
    ArtifactStore store;
    QString err;
    REQUIRE( store.open( dir.filePath( "artifacts.sqlite" ), &err ) );

    const auto missing = store.registerArtifact(
        makeRegistration( QStringLiteral( "ghost" ), dir.filePath( "does-not-exist.tif" ) ) );
    REQUIRE_FALSE( missing );
    REQUIRE( missing.diagnostics().first().code == QStringLiteral( "artifact.payload_missing" ) );
    REQUIRE( store.count() == 0 );
}

TEST_CASE( "Content digest helper matches SHA-256 of file bytes", "[artifact_store][digest]" )
{
    QTemporaryDir dir;
    const QString payload = dir.filePath( "blob" );
    const QByteArray bytes( "deterministic content for hashing" );
    writePayload( payload, bytes );

    const QString digest = artifactContentDigest( payload );
    const QByteArray expected = QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex();
    REQUIRE( digest == QString::fromLatin1( expected ) );

    QString err;
    REQUIRE( artifactContentDigest( dir.filePath( "missing" ), &err ).isEmpty() );
    REQUIRE_FALSE( err.isEmpty() );
}

TEST_CASE( "References pin artifacts and block reaping (invariant I7)", "[artifact_store][gc]" )
{
    QTemporaryDir dir;
    const QString payload = dir.filePath( "a.tif" );
    writePayload( payload, "payload" );

    ArtifactStore store;
    QString err;
    REQUIRE( store.open( dir.filePath( "artifacts.sqlite" ), &err ) );
    const auto rec = store.registerArtifact( makeRegistration( QStringLiteral( "x" ), payload ) );
    REQUIRE( rec );
    const QString id = rec.value().artifactId;

    REQUIRE( store.attachRef( id, QStringLiteral( "data_asset" ), QStringLiteral( "asset-1" ) ) );
    REQUIRE( store.attachRef( id, QStringLiteral( "cache_lease" ), QStringLiteral( "lease-9" ) ) );
    REQUIRE( store.refCount( id ) == 2 );
    REQUIRE( store.refsOf( id ).size() == 2 );

    // Trashed but still referenced → not reapable, even when stale.
    REQUIRE( store.markTrash( id ) );
    REQUIRE( store.reapable( QDateTime::currentMSecsSinceEpoch() + 1000 ).isEmpty() );

    // Drain one lease: still pinned.
    REQUIRE( store.detachRef( id, QStringLiteral( "cache_lease" ), QStringLiteral( "lease-9" ) ) );
    REQUIRE( store.reapable( QDateTime::currentMSecsSinceEpoch() + 1000 ).isEmpty() );

    // Fully unreferenced + stale → reapable.
    REQUIRE( store.detachRef( id, QStringLiteral( "data_asset" ), QStringLiteral( "asset-1" ) ) );
    const auto reapable = store.reapable( QDateTime::currentMSecsSinceEpoch() + 1000 );
    REQUIRE( reapable.size() == 1 );
    REQUIRE( reapable.first().artifactId == id );

    // Touch bumps lastTouch so freshness is honored.
    REQUIRE( store.touch( id ) );
    REQUIRE( store.reapable( QDateTime::currentMSecsSinceEpoch() ).isEmpty() );

    // Live artifacts are never reapable regardless of refs/age.
    REQUIRE( store.retain( id ).diagnostics().first().code == QStringLiteral( "artifact.state" ) );
    const auto liveRec = store.registerArtifact( makeRegistration( QStringLiteral( "y" ), payload ) );
    REQUIRE( liveRec );
    REQUIRE( store.reapable( QDateTime::currentMSecsSinceEpoch() + 1000 ).size() == 1 );
}

TEST_CASE( "forget removes metadata after physical deletion", "[artifact_store][gc]" )
{
    QTemporaryDir dir;
    const QString payload = dir.filePath( "a.tif" );
    writePayload( payload, "payload" );

    ArtifactStore store;
    QString err;
    REQUIRE( store.open( dir.filePath( "artifacts.sqlite" ), &err ) );
    const auto rec = store.registerArtifact( makeRegistration( QStringLiteral( "x" ), payload ) );
    REQUIRE( rec );
    const QString id = rec.value().artifactId;
    REQUIRE( store.attachRef( id, QStringLiteral( "run" ), QStringLiteral( "run-1" ) ) );

    REQUIRE( store.forget( id ) ); // cascade drops refs too
    REQUIRE( store.count() == 0 );
    REQUIRE( store.artifactById( id ) == std::nullopt );
    REQUIRE( store.refsOf( id ).isEmpty() );
    REQUIRE_FALSE( store.forget( id ) );
}

TEST_CASE( "Digest backfill and content-digest lookup", "[artifact_store][digest]" )
{
    QTemporaryDir dir;
    const QString payload = dir.filePath( "a.tif" );
    const QByteArray bytes( "shared-bytes" );
    writePayload( payload, bytes );

    ArtifactStore store;
    QString err;
    REQUIRE( store.open( dir.filePath( "artifacts.sqlite" ), &err ) );
    const auto rec = store.registerArtifact( makeRegistration( QStringLiteral( "x" ), payload ) );
    REQUIRE( rec );
    REQUIRE( rec.value().contentDigest.isEmpty() ); // not provided

    const QString digest = artifactContentDigest( payload );
    REQUIRE( store.updateContentDigest( rec.value().artifactId, digest ) );
    REQUIRE_FALSE( store.updateContentDigest( QStringLiteral( "nope" ), digest ) );

    const auto byDigest = store.liveByContentDigest( digest );
    REQUIRE( byDigest );
    REQUIRE( byDigest->artifactId == rec.value().artifactId );

    // Trashed artifacts are not live-digest matches.
    REQUIRE( store.markTrash( rec.value().artifactId ) );
    REQUIRE( store.liveByContentDigest( digest ) == std::nullopt );
}

TEST_CASE( "ArtifactStore refuses a newer-schema database", "[artifact_store][migration]" )
{
    QTemporaryDir dir;
    const QString db = dir.filePath( "artifacts.sqlite" );
    {
        ArtifactStore store;
        QString err;
        REQUIRE( store.open( db, &err ) );
    }
    // Simulate a future schema bump (direct DB surgery, like a newer build would).
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( db.toUtf8().constData(), &raw,
                                  SQLITE_OPEN_READWRITE, nullptr ) == SQLITE_OK );
        char *err = nullptr;
        REQUIRE( sqlite3_exec( raw,
                               "UPDATE store_meta SET value='99' WHERE key='schema_version'",
                               nullptr, nullptr, &err ) == SQLITE_OK );
        sqlite3_free( err );
        sqlite3_close( raw );
    }
    ArtifactStore store;
    QString err;
    REQUIRE_FALSE( store.open( db, &err ) );
    REQUIRE( err.contains( QStringLiteral( "newer" ) ) );
}

TEST_CASE( "ArtifactStore survives concurrent store handles (WAL)", "[artifact_store][crash]" )
{
    QTemporaryDir dir;
    const QString db = dir.filePath( "artifacts.sqlite" );
    ArtifactStore writer;
    QString err;
    REQUIRE( writer.open( db, &err ) );
    const QString payload = dir.filePath( "a.tif" );
    writePayload( payload, "payload" );
    const auto rec = writer.registerArtifact( makeRegistration( QStringLiteral( "x" ), payload ) );
    REQUIRE( rec );

    // A second handle (fresh open, like a recovering process) sees the commit.
    ArtifactStore reader;
    REQUIRE( reader.open( db, &err ) );
    const auto seen = reader.artifactById( rec.value().artifactId );
    REQUIRE( seen );
    REQUIRE( seen->logicalKey == QStringLiteral( "x" ) );
}
