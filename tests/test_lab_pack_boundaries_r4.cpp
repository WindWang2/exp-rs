/***************************************************************************
  tests/test_lab_pack_boundaries_r4.cpp — WP-A (Track 12, teaching-lab R4):
  the EOL / encoding / asset / manifest BOUNDARY MATRIX for the pack chain.

  Every boundary class has ONE decided semantics (DECISIONS.md B01..B22):
  REJECT with a typed message naming the asset/field/reason, or NORMALIZE to
  a canonical form whose digest/size is stable. Truth is INDEPENDENT of the
  implementation: pins are computed in this file from the git canonical-bytes
  rule (NUL in first 8000 ⇒ binary as-is; else CRLF→LF, lone CR kept) — the
  rule the repo's committed pins were produced with — never by calling the
  code under test.

  Entries:
    sicnu::labpack::PackVerifier::loadFromBytes  — the ONE pack parser
    sicnu::teaching_admin::inventoryPacks        — the admin inventory entry
    sicnu::teaching_admin::canonicalFileSha256/Size — direct unit pins
    (linked via Sicnu::teaching_admin's PUBLIC sicnu_lab_pack)

    The #1336 first-round semantics (canonical-bytes pins) must NOT
    regress: B01/B02/B17 pin it from the outside. Chunk-boundary cases
    (CHUNK-1/2/3) exercise the streaming pending-CR logic directly against
    the authority's 64KiB-from-offset-0 reads — the delegate of both the
    admin entry and the verifier since the round-2 mirror removal.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "lab_pack/lab_pack.h"
#include "teaching_admin/data_pack_manager.h"
#include "teaching_admin/json_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QTemporaryDir>

#include <algorithm>
#include <string>
#include <vector>

using namespace sicnu::teaching_admin;

namespace
{

/// 64 lowercase hex chars — the shape the authority pins (any value works
/// where only the typed rejection is under test).
constexpr const char *kAnySha = "ad081257ef39112d0af3207f1697a3b2ad89042e2ca728e14864b7cdaac4467d";

/// Independent oracle: the git canonical bytes of @p raw (Python
/// canonical_bytes() twin). NUL anywhere in the first 8000 bytes ⇒ binary
/// (as-is); else CRLF→LF, lone CR kept.
QByteArray canonicalBytes( const QByteArray &raw )
{
    const int probe = static_cast<int>( std::min<qsizetype>( 8000, raw.size() ) );
    for ( int i = 0; i < probe; ++i )
        if ( raw[i] == '\0' )
            return raw;
    QByteArray out;
    out.reserve( raw.size() );
    for ( int i = 0; i < raw.size(); ++i )
    {
        if ( raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n' )
            continue; // drop the CR of a CRLF pair
        out.append( raw[i] );
    }
    return out;
}

QString oracleSha256( const QByteArray &bytes )
{
    return QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

void writeBytes( const QString &path, const QByteArray &bytes )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( bytes );
}

struct InventoryFixture
{
    QTemporaryDir root;
    QDir packs;

    InventoryFixture()
    {
        packs = root.filePath( QStringLiteral( "packs" ) );
        REQUIRE( QDir().mkpath( packs.path() ) );
        REQUIRE( QDir().mkpath( root.filePath( QStringLiteral( "data" ) ) ) );
    }

    void writePack( const QByteArray &doc )
    {
        writeBytes( packs.filePath( QStringLiteral( "b.pack.json" ) ), doc );
    }

    PackInventory inventory() { return inventoryPacks( packs.path(), root.path() ); }
};

/// One committed-fixture pack around @p assetRel with @p sha and @p bytes.
QByteArray packWith( const QString &assetRel, const char *sha, qint64 bytes,
                     const char *provenance = "committed-fixture" )
{
    return QStringLiteral(
             R"JSON({
               "schema_version": "sicnu.lab-pack/1",
               "lab_id": "lab90_r4",
               "pack_version": "1.0.0",
               "license": "test",
               "declared_offline_bytes": %1,
               "inputs": [ {
                 "path": "%2",
                 "role": "aux",
                 "provenance": "%3",
                 "sha256": "%4",
                 "bytes": %1
               } ]
             })JSON" )
               .arg( bytes )
               .arg( assetRel, QString::fromLatin1( provenance ), QString::fromLatin1( sha ) )
               .toUtf8();
}

bool hasIssue( const PackInventoryEntry &e, const QString &code, const QString &severity,
               const QString &needle )
{
    for ( const AdminIssue &issue : e.issues )
        if ( issue.code == code && issue.severity == severity && issue.message.contains( needle ) )
            return true;
    return false;
}

bool hasIssueAtPath( const PackInventoryEntry &e, const QString &code, const QString &severity,
                     const QString &path )
{
    for ( const AdminIssue &issue : e.issues )
        if ( issue.code == code && issue.severity == severity && issue.path == path )
            return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Schema / parse boundaries (the ONE authority parser, in-memory bytes)
// ---------------------------------------------------------------------------

TEST_CASE( "boundary B21: trailing comma is a rejected pack schema",
           "[teaching_r4][boundary][schema]" )
{
    const sicnu::labpack::PackLoadResult leaf = sicnu::labpack::PackVerifier::loadFromBytes(
      R"({"schema_version":"sicnu.lab-pack/1","lab_id":"x",})", "b" );
    CHECK_FALSE( leaf.ok );
    // jsoncpp's recovery point decides whether the mangled tail reads as a
    // parse error (pack_schema) or as a doc missing required fields
    // (pack_field) — both are typed rejections, never a silent accept.
    CHECK( ( leaf.errorCode == "lab.pack_schema" || leaf.errorCode == "lab.pack_field" ) );
}

TEST_CASE( "boundary B04: UTF-16 bytes cannot masquerade as a pack document",
           "[teaching_r4][boundary][schema]" )
{
    // "{\"schema_version\"...}" encoded UTF-16LE with BOM — the authority
    // must refuse, not mis-read.
    const QByteArray utf16 = QStringLiteral( "{\"schema_version\":\"sicnu.lab-pack/1\"}" )
                               .toUtf8(); // source text
    QByteArray bytes;
    bytes.append( '\xFF' ).append( '\xFE' );
    for ( char c : utf16 )
    {
        bytes.append( c ).append( '\0' );
    }
    const sicnu::labpack::PackLoadResult leaf =
      sicnu::labpack::PackVerifier::loadFromBytes( std::string( bytes.constData(), bytes.size() ), "b" );
    CHECK_FALSE( leaf.ok );
    CHECK( leaf.errorCode == "lab.pack_schema" );
}

TEST_CASE( "boundary B11: duplicate input paths are rejected with the field named",
           "[teaching_r4][boundary][manifest]" )
{
    const sicnu::labpack::PackLoadResult leaf = sicnu::labpack::PackVerifier::loadFromBytes(
      R"JSON({
        "schema_version": "sicnu.lab-pack/1",
        "lab_id": "x", "pack_version": "1.0.0", "license": "t",
        "inputs": [
          { "path": "data/a.tif", "role": "sample", "provenance": "generated-samples" },
          { "path": "data/a.tif", "role": "sample", "provenance": "generated-samples" }
        ]
      })JSON", "b" );
    CHECK_FALSE( leaf.ok );
    CHECK( leaf.errorCode == "lab.pack_input" );
}

TEST_CASE( "boundary B09: committed fixture without sha256 is rejected",
           "[teaching_r4][boundary][manifest]" )
{
    const sicnu::labpack::PackLoadResult leaf = sicnu::labpack::PackVerifier::loadFromBytes(
      R"JSON({
        "schema_version": "sicnu.lab-pack/1",
        "lab_id": "x", "pack_version": "1.0.0", "license": "t",
        "inputs": [ { "path": "data/a.tif", "role": "aux", "provenance": "committed-fixture" } ]
      })JSON", "b" );
    CHECK_FALSE( leaf.ok );
    CHECK( leaf.errorCode == "lab.pack_input" );
}

// ---------------------------------------------------------------------------
// EOL / encoding boundaries (normalize — the #1336 rule, pinned from outside)
// ---------------------------------------------------------------------------

TEST_CASE( "boundary B01: CRLF text fixture normalizes — LF pins hold on a CRLF disk",
           "[teaching_r4][boundary][eol]" )
{
    InventoryFixture fx;
    // The fixture as a Windows checkout would put it on disk:
    const QByteArray onDisk = "line1\r\nline2\r\n";
    writeBytes( fx.root.filePath( QStringLiteral( "data/fixture.txt" ) ), onDisk );
    // The pin the foundry committed: GIT canonical bytes (LF) — truth from
    // this file's own oracle, not from the implementation.
    const QString pin = oracleSha256( canonicalBytes( onDisk ) );

    fx.writePack( packWith( QStringLiteral( "data/fixture.txt" ), pin.toLatin1().constData(),
                            canonicalBytes( onDisk ).size() ) );
    const PackInventory inv = fx.inventory();
    REQUIRE( inv.packs.size() == 1 );
    CHECK_FALSE( hasIssue( inv.packs.front(), QStringLiteral( "digest_mismatch" ),
                           QStringLiteral( "error" ), QString() ) );
    CHECK( inv.packs.front().offlineAvailable );
}

TEST_CASE( "boundary B02: lone CR (classic Mac) is kept, not rewritten",
           "[teaching_r4][boundary][eol]" )
{
    InventoryFixture fx;
    const QByteArray onDisk = "line1\rline2\r"; // CR-only — canonical bytes are IDENTICAL
    writeBytes( fx.root.filePath( QStringLiteral( "data/fixture.txt" ) ), onDisk );
    const QString pin = oracleSha256( canonicalBytes( onDisk ) );

    fx.writePack( packWith( QStringLiteral( "data/fixture.txt" ), pin.toLatin1().constData(),
                            canonicalBytes( onDisk ).size() ) );
    const PackInventory inv = fx.inventory();
    REQUIRE( inv.packs.size() == 1 );
    CHECK( inv.packs.front().offlineAvailable );
}

TEST_CASE( "boundary B17/B08: committed byte pins reject zero-byte and drifted assets",
           "[teaching_r4][boundary][bytes]" )
{
    InventoryFixture fx;
    const QByteArray onDisk = "real-bytes";
    writeBytes( fx.root.filePath( QStringLiteral( "data/a.bin" ) ), onDisk );
    const QString pin = oracleSha256( canonicalBytes( onDisk ) );

    // Drifted declared size (canonical口径) → hard error for a committed tier.
    fx.writePack( packWith( QStringLiteral( "data/a.bin" ), pin.toLatin1().constData(),
                            onDisk.size() + 7 ) );
    PackInventory inv = fx.inventory();
    CHECK( hasIssue( inv.packs.front(), QStringLiteral( "byte_mismatch" ), QStringLiteral( "error" ),
                     QStringLiteral( "declared" ) ) );

    // Zero-byte asset against a non-zero pin → the same hard rejection.
    QFile zero( fx.root.filePath( QStringLiteral( "data/a.bin" ) ) );
    REQUIRE( zero.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    inv = fx.inventory();
    CHECK( hasIssue( inv.packs.front(), QStringLiteral( "byte_mismatch" ), QStringLiteral( "error" ),
                     QStringLiteral( "actual 0" ) ) );
}

// ---------------------------------------------------------------------------
// Asset presence / type / path boundaries
// ---------------------------------------------------------------------------

TEST_CASE( "boundary B06: missing committed asset is a named hard rejection",
           "[teaching_r4][boundary][assets]" )
{
    InventoryFixture fx;
    fx.writePack( packWith( QStringLiteral( "data/absent.tif" ), kAnySha, 1881 ) );
    const PackInventory inv = fx.inventory();
    REQUIRE( inv.packs.size() == 1 );
    CHECK( hasIssue( inv.packs.front(), QStringLiteral( "input_missing" ), QStringLiteral( "error" ),
                     QStringLiteral( "absent.tif" ) ) );
    CHECK_FALSE( inv.packs.front().offlineAvailable );
}

TEST_CASE( "boundary B07/B16: missing / digest-drifted GENERATED assets degrade with warnings",
           "[teaching_r4][boundary][assets]" )
{
    InventoryFixture fx;
    fx.writePack( packWith( QStringLiteral( "data/regen.tif" ), kAnySha, 100,
                            "generated-samples" ) );
    const PackInventory inv = fx.inventory();
    REQUIRE( inv.packs.size() == 1 );
    // Regenerable tier: missing input is a WARNING (severity), and a bogus
    // declared digest cannot error the tier either (strength follows tier).
    CHECK( hasIssue( inv.packs.front(), QStringLiteral( "input_missing" ),
                     QStringLiteral( "warning" ), QStringLiteral( "regen.tif" ) ) );
}

TEST_CASE( "boundary B10: a directory in the asset slot is not silently present",
           "[teaching_r4][boundary][assets]" )
{
    InventoryFixture fx;
    QDir().mkpath( fx.root.filePath( QStringLiteral( "data/not_a_file.tif" ) ) );
    fx.writePack( packWith( QStringLiteral( "data/not_a_file.tif" ), kAnySha, 10 ) );
    const PackInventory inv = fx.inventory();
    CHECK( hasIssue( inv.packs.front(), QStringLiteral( "input_missing" ), QStringLiteral( "error" ),
                     QStringLiteral( "not_a_file.tif" ) ) );
    CHECK_FALSE( inv.packs.front().offlineAvailable );
}

TEST_CASE( "boundary B13: traversal-shaped input paths are rejected twice over",
           "[teaching_r4][boundary][paths]" )
{
    InventoryFixture fx;
    const QByteArray doc = R"JSON({
      "schema_version": "sicnu.lab-pack/1",
      "lab_id": "x", "pack_version": "1.0.0", "license": "t",
      "inputs": [ { "path": "../escape.tif", "role": "aux", "provenance": "generated-samples" } ]
    })JSON";
    fx.writePack( doc );
    const PackInventory inv = fx.inventory();
    CHECK( hasIssue( inv.packs.front(), QStringLiteral( "path_traversal" ), QStringLiteral( "error" ),
                     QStringLiteral( "escape" ) ) );
}

TEST_CASE( "boundary B14: symlink escape from inside the root is a typed path_escape",
           "[teaching_r4][boundary][paths]" )
{
    InventoryFixture fx;
    const QTemporaryDir outside;
    writeBytes( outside.filePath( QStringLiteral( "real.tif" ) ), "x" );
    const QString link = fx.root.filePath( QStringLiteral( "data" ) );
    REQUIRE( QDir().mkpath( link ) );
    REQUIRE( QFile::link( outside.filePath( QStringLiteral( "real.tif" ) ),
                          link + QStringLiteral( "/jail_break.tif" ) ) );
    fx.writePack( packWith( QStringLiteral( "data/jail_break.tif" ), kAnySha, 1 ) );
    const PackInventory inv = fx.inventory();
    CHECK( hasIssue( inv.packs.front(), QStringLiteral( "path_escape" ), QStringLiteral( "error" ),
                     QStringLiteral( "repo root" ) ) );
}

TEST_CASE( "boundary B20: an unopenable over-long asset path degrades typed, not crash",
           "[teaching_r4][boundary][paths]" )
{
    InventoryFixture fx;
    QString deep = QStringLiteral( "data" );
    for ( int i = 0; i < 512; ++i ) // ≈5.7 kB — beyond PATH_MAX(4096)
        deep += QStringLiteral( "/segment_%1" ).arg( i );
    fx.writePack( packWith( deep + QStringLiteral( "/leaf.tif" ), kAnySha, 1 ) );
    const PackInventory inv = fx.inventory();
    REQUIRE( inv.packs.size() == 1 );
    CHECK_FALSE( inv.packs.front().offlineAvailable );
    CHECK_FALSE( inv.packs.front().issues.isEmpty() );
}

TEST_CASE( "boundary B12: case-variant asset names are distinct entries, never merged",
           "[teaching_r4][boundary][names]" )
{
    InventoryFixture fx;
    writeBytes( fx.root.filePath( QStringLiteral( "data/Scene.tif" ) ), "upper-bytes" );
    writeBytes( fx.root.filePath( QStringLiteral( "data/scene.tif" ) ), "lower-bytes" );

    const QByteArray doc = QStringLiteral(
                             R"JSON({
                               "schema_version": "sicnu.lab-pack/1",
                               "lab_id": "x", "pack_version": "1.0.0", "license": "t",
                               "inputs": [
                                 { "path": "data/Scene.tif", "role": "aux", "provenance": "committed-fixture", "sha256": "%1", "bytes": 11 },
                                 { "path": "data/scene.tif", "role": "aux", "provenance": "committed-fixture", "sha256": "%2", "bytes": 11 }
                               ]
                             })JSON" )
                             .arg( oracleSha256( "upper-bytes" ), oracleSha256( "lower-bytes" ) )
                             .toUtf8();
    fx.writePack( doc );
    const PackInventory inv = fx.inventory();
    REQUIRE( inv.packs.size() == 1 );
    // Byte-exact identity: no duplicate-path rejection, no merge, both honest.
    CHECK( inv.packs.front().issues.isEmpty() );
    CHECK( inv.packs.front().offlineAvailable );
}

// ---------------------------------------------------------------------------
// Name-identity boundaries
// ---------------------------------------------------------------------------

TEST_CASE( "boundary B05/B12: NFC/NFD and case variants are distinct byte paths",
           "[teaching_r4][boundary][names]" )
{
    InventoryFixture fx;
    // Two DIFFERENT byte-strings that a normalizing or case-folding loader
    // would merge; both committed, both present, both pins honest.
    const QString nfd = QString::fromUtf8( "e\u0301clair.tif" ); // NFD
    const QString nfc = QString::fromUtf8( "\xC3\xA9clair.tif" ); // NFC (É precomposed)
    REQUIRE( nfc != nfd );
    writeBytes( fx.root.filePath( nfd ), "nfd-bytes" );
    writeBytes( fx.root.filePath( nfc ), "nfc-bytes" );

    const QByteArray doc = QStringLiteral(
                             R"JSON({
                               "schema_version": "sicnu.lab-pack/1",
                               "lab_id": "x", "pack_version": "1.0.0", "license": "t",
                               "inputs": [
                                 { "path": "%1", "role": "aux", "provenance": "committed-fixture", "sha256": "%2", "bytes": 9 },
                                 { "path": "%3", "role": "aux", "provenance": "committed-fixture", "sha256": "%4", "bytes": 9 }
                               ]
                             })JSON" )
                             .arg( nfd, oracleSha256( "nfd-bytes" ),
                                   nfc, oracleSha256( "nfc-bytes" ) )
                             .toUtf8();
    fx.writePack( doc );
    const PackInventory inv = fx.inventory();
    REQUIRE( inv.packs.size() == 1 );
    const PackInventoryEntry &entry = inv.packs.front();
    CHECK( entry.issues.isEmpty() ); // no duplicate merge, no digest complaint
    CHECK( entry.offlineAvailable );
}

// ---------------------------------------------------------------------------
// Pack-level manifest boundaries
// ---------------------------------------------------------------------------

TEST_CASE( "boundary B19: pack-level declared_offline_bytes drift is an honest warning",
           "[teaching_r4][boundary][manifest]" )
{
    InventoryFixture fx;
    const QByteArray onDisk = "12345";
    writeBytes( fx.root.filePath( QStringLiteral( "data/a.bin" ) ), onDisk );
    const QString pin = oracleSha256( canonicalBytes( onDisk ) );
    fx.writePack( packWith( QStringLiteral( "data/a.bin" ), pin.toLatin1().constData(),
                            onDisk.size() + 99 ) );
    const PackInventory inv = fx.inventory();
    CHECK( hasIssueAtPath( inv.packs.front(), QStringLiteral( "byte_mismatch" ),
                           QStringLiteral( "warning" ), QStringLiteral( "declared_offline_bytes" ) ) );
}

TEST_CASE( "boundary B22: an empty packs directory is an empty inventory, not an error",
           "[teaching_r4][boundary][manifest]" )
{
    const QTemporaryDir root;
    const PackInventory inv = inventoryPacks( root.filePath( QStringLiteral( "packs" ) ),
                                              root.path() );
    CHECK( inv.packs.isEmpty() );
    CHECK( inv.totalDeclaredBytes == 0 );
    CHECK( inv.withinBudget );
}

TEST_CASE( "boundary B15: committed digest drift is a named hard rejection (no canonical bypass)",
           "[teaching_r4][boundary][manifest]" )
{
    InventoryFixture fx;
    const QByteArray onDisk = "true content";
    writeBytes( fx.root.filePath( QStringLiteral( "data/a.bin" ) ), onDisk );
    // Pin over DIFFERENT bytes: the deployment is not the audited one.
    fx.writePack( packWith( QStringLiteral( "data/a.bin" ),
                            oracleSha256( QByteArray( "audited content" ) ).toLatin1().constData(),
                            onDisk.size() ) );
    const PackInventory inv = fx.inventory();
    CHECK( hasIssue( inv.packs.front(), QStringLiteral( "digest_mismatch" ), QStringLiteral( "error" ),
                     QStringLiteral( "declared" ) ) );
    CHECK_FALSE( inv.packs.front().offlineAvailable );
}

// ---------------------------------------------------------------------------
// Direct canonical-function pins (chunking + probe boundary) — P1-2/R3
// ---------------------------------------------------------------------------

TEST_CASE( "boundary CHUNK-1: CRLF spanning the 64KiB chunk boundary normalizes",
           "[teaching_r4][boundary][chunking]" )
{
    QTemporaryDir root;
    // The authority (sicnu::labpack canonical digest, which the admin entry
    // now delegates to) streams 64KiB reads from offset 0, so the pending-CR
    // edge case is a CR as the LAST byte of chunk 1 with its LF opening
    // chunk 2. (While the admin mirror existed — removed in favour of the
    // delegation — its 8000-byte head shifted the boundary by 8000; the
    // fixture pins the surviving, authoritative chunking.)
    QByteArray raw;
    raw.reserve( 65536 + 16 + 1 );
    raw.append( QByteArray( 65535, 'a' ) );
    raw.append( '\r' );    // last byte of chunk 1
    raw.append( "\nmid" ); // the LF opens chunk 2
    raw.append( QByteArray( 8, 'b' ) );
    raw.append( '\r' );    // lone CR at EOF — kept

    const QString path = root.filePath( QStringLiteral( "chunked.txt" ) );
    writeBytes( path, raw );

    // The oracle normalizes the WHOLE content in one pass (no chunking);
    // equality proves the streamed pending-CR logic is equivalent.
    CHECK( canonicalFileSha256( path ) == oracleSha256( canonicalBytes( raw ) ) );
    CHECK( canonicalFileSize( path ) == static_cast<qint64>( canonicalBytes( raw ).size() ) );
    // The mid-file CRLF normalized away (2 bytes → 1), the trailing lone CR kept:
    CHECK( canonicalFileSize( path ) == static_cast<qint64>( raw.size() - 1 ) );
}

TEST_CASE( "boundary CHUNK-2: NUL probe edge decides binary vs text classification",
           "[teaching_r4][boundary][chunking]" )
{
    QTemporaryDir root;
    // Classification is only observable when a CRLF follows: text mode
    // collapses it, binary mode keeps it. git probes the FIRST 8000 bytes.
    // A: NUL at index 8000 (outside the window) -> TEXT -> CRLF collapses.
    QByteArray rawA( 8000, 't' );
    rawA.append( '\0' );
    rawA.append( "A\r\nB" );
    // B: NUL at index 7999 (inside the window) -> BINARY -> CRLF kept raw.
    QByteArray rawB( 7999, 't' );
    rawB.append( '\0' );
    rawB.append( "A\r\nB" );

    const QString pathA = root.filePath( QStringLiteral( "edge_a.bin" ) );
    const QString pathB = root.filePath( QStringLiteral( "edge_b.bin" ) );
    writeBytes( pathA, rawA );
    writeBytes( pathB, rawB );

    CHECK( canonicalFileSha256( pathA ) == oracleSha256( canonicalBytes( rawA ) ) );
    CHECK( canonicalFileSize( pathA ) == static_cast<qint64>( canonicalBytes( rawA ).size() ) );
    // The oracle classifies rawB binary too (NUL within its 8000-byte probe)
    // — both sides keep the CRLF, and A vs B digests must DIFFER.
    CHECK( canonicalFileSha256( pathB ) == oracleSha256( rawB ) );
    CHECK( canonicalFileSha256( pathA ) != canonicalFileSha256( pathB ) );
}

TEST_CASE( "boundary CHUNK-3: empty and all-CR files have stable canonical identity",
           "[teaching_r4][boundary][chunking]" )
{
    QTemporaryDir root;
    const QString emptyPath = root.filePath( QStringLiteral( "empty.bin" ) );
    writeBytes( emptyPath, QByteArray() );
    CHECK( canonicalFileSha256( emptyPath ) == oracleSha256( QByteArray() ) );
    CHECK( canonicalFileSize( emptyPath ) == 0 );

    const QString crPath = root.filePath( QStringLiteral( "allcr.txt" ) );
    const QByteArray allCr = QByteArray( 100, '\r' );
    writeBytes( crPath, allCr );
    // Lone CRs are content: canonical == raw, nothing rewritten.
    CHECK( canonicalFileSha256( crPath ) == oracleSha256( allCr ) );
    CHECK( canonicalFileSize( crPath ) == 100 );
}

TEST_CASE( "boundary B03: a UTF-8 BOM is content — it participates in the pin",
           "[teaching_r4][boundary][encoding]" )
{
    InventoryFixture fx;
    // BOM + CRLF text: the BOM bytes are not EOL — canonicalization keeps
    // them, only the CRLF collapses.
    const QByteArray onDisk = "\xEF\xBB\xBF"
                              "line1\r\nline2\r\n";
    writeBytes( fx.root.filePath( QStringLiteral( "data/bom.txt" ) ), onDisk );
    const QString pin = sha256Hex( canonicalBytes( onDisk ) );

    fx.writePack( packWith( QStringLiteral( "data/bom.txt" ), pin.toLatin1().constData(),
                            canonicalBytes( onDisk ).size() ) );
    const PackInventory inv = fx.inventory();
    REQUIRE( inv.packs.size() == 1 );
    CHECK_FALSE( hasIssue( inv.packs.front(), QStringLiteral( "digest_mismatch" ),
                           QStringLiteral( "error" ), QString() ) );
    CHECK( inv.packs.front().offlineAvailable );
}

TEST_CASE( "boundary B18: generated-tier byte drift degrades as a warning, not an error",
           "[teaching_r4][boundary][bytes]" )
{
    InventoryFixture fx;
    const QByteArray onDisk = "regen-bytes";
    writeBytes( fx.root.filePath( QStringLiteral( "data/r.tif" ) ), onDisk );
    const QString pin = sha256Hex( canonicalBytes( onDisk ) );

    fx.writePack( packWith( QStringLiteral( "data/r.tif" ), pin.toLatin1().constData(),
                            onDisk.size() + 55, "generated-samples" ) );
    const PackInventory inv = fx.inventory();
    CHECK( hasIssue( inv.packs.front(), QStringLiteral( "byte_mismatch" ), QStringLiteral( "warning" ),
                     QStringLiteral( "declared" ) ) );
    CHECK_FALSE( hasIssue( inv.packs.front(), QStringLiteral( "byte_mismatch" ), QStringLiteral( "error" ),
                           QString() ) );
}
// ---------------------------------------------------------------------------
// Authority direct surface (round-2 遗留3): the SAME EOL/encoding boundary
// classes pinned at sicnu::labpack::PackVerifier::verify itself — not through
// the admin projection. #1336's canonical-bytes semantics live here; if the
// admin layer ever grows another mirror, these rows keep the authority's own
// verdict on record and the divergence becomes testable from both sides.
// ---------------------------------------------------------------------------

namespace
{

/// Load a pack document through the ONE authority parser.
sicnu::labpack::PackDocument loadedPack( const QString &docPath )
{
    const sicnu::labpack::PackLoadResult r = sicnu::labpack::PackVerifier::load(
      sicnu::labpack::pathFromUtf8( docPath.toStdString() ) );
    REQUIRE( r.ok );
    return r.pack;
}

/// One committed-fixture layout for direct verify() calls: <root>/<assetRel>
/// holds @p onDisk; the pack pins the CANONICAL bytes of @p pinnedContent
/// (truth from this file's oracle, never from the implementation).
struct DirectPack
{
    QTemporaryDir root;
    sicnu::labpack::PackDocument doc;

    DirectPack( const QString &assetRel, const QByteArray &onDisk,
                const QByteArray &pinnedContent )
    {
        const QString abs = root.filePath( assetRel );
        REQUIRE( QDir().mkpath( QFileInfo( abs ).absolutePath() ) );
        writeBytes( abs, onDisk );

        const QString docPath = root.filePath( QStringLiteral( "b.pack.json" ) );
        writeBytes( docPath, packWith( assetRel,
                                       oracleSha256( canonicalBytes( pinnedContent ) ).toLatin1().constData(),
                                       canonicalBytes( pinnedContent ).size() ) );
        doc = loadedPack( docPath );
    }

    sicnu::labpack::PackVerification verify()
    {
        return sicnu::labpack::PackVerifier::verify(
          doc, sicnu::labpack::pathFromUtf8( root.path().toStdString() ) );
    }
};

bool pvHasIssue( const sicnu::labpack::PackVerification &v, const char *code,
                 const char *pathNeedle )
{
    for ( const Json::Value &issue : v.issues )
        if ( issue["code"].asString() == code
             && issue["path"].asString().find( pathNeedle ) != std::string::npos )
            return true;
    return false;
}

} // namespace

TEST_CASE( "authority PV-B01: CRLF fixture verifies against LF pins at PackVerifier itself",
           "[teaching_r4][boundary][authority][eol]" )
{
    DirectPack fx( QStringLiteral( "data/fixture.txt" ), "line1\r\nline2\r\n", "line1\r\nline2\r\n" );
    const sicnu::labpack::PackVerification v = fx.verify();
    INFO( "overall=" << v.overall );
    REQUIRE( v.overall == "verified" );
    REQUIRE( v.issues.empty() );
    REQUIRE( v.verifiedBytes == static_cast<std::int64_t>( canonicalBytes( "line1\r\nline2\r\n" ).size() ) );
}

TEST_CASE( "authority PV-B02: lone-CR fixture verifies — CRs are content at the authority too",
           "[teaching_r4][boundary][authority][eol]" )
{
    DirectPack fx( QStringLiteral( "data/fixture.txt" ), "a\rb\rc\r", "a\rb\rc\r" );
    const sicnu::labpack::PackVerification v = fx.verify();
    REQUIRE( v.overall == "verified" );
    REQUIRE( v.issues.empty() );
}

TEST_CASE( "authority PV-B03: a BOM participates in the pin — present and absent pins both typed",
           "[teaching_r4][boundary][authority][encoding]" )
{
    const QByteArray withBom = "\xEF\xBB\xBF" "plan\r\n";

    // Pin over the real bytes (BOM included) → verified.
    {
        DirectPack fx( QStringLiteral( "data/bom.json" ), withBom, withBom );
        REQUIRE( fx.verify().overall == "verified" );
    }
    // Pin computed as if the BOM were not content → hard checksum rejection
    // naming the asset (the authority never strips a BOM to make pins fit).
    {
        DirectPack fx( QStringLiteral( "data/bom.json" ), withBom, "plan\r\n" );
        const sicnu::labpack::PackVerification v = fx.verify();
        REQUIRE( v.overall == "failed" );
        // The size check precedes the digest check for committed fixtures, so
        // pinning the BOM-less bytes names lab.pack_size_mismatch — the byte
        // count is part of the pin, which is the point: the BOM is content.
        REQUIRE( pvHasIssue( v, "lab.pack_size_mismatch", "bom.json" ) );
    }
}

TEST_CASE( "authority PV-CHUNK: CRLF straddling the authority's own 64KiB chunk edge normalizes",
           "[teaching_r4][boundary][authority][chunking]" )
{
    QByteArray raw;
    raw.reserve( 65536 + 16 + 1 );
    raw.append( QByteArray( 65535, 'a' ) );
    raw.append( '\r' );    // last byte of the authority's chunk 1
    raw.append( "\nmid" ); // LF opens chunk 2
    raw.append( QByteArray( 8, 'b' ) );
    raw.append( '\r' );    // lone CR at EOF

    DirectPack fx( QStringLiteral( "data/chunked.txt" ), raw, raw );
    const sicnu::labpack::PackVerification v = fx.verify();
    REQUIRE( v.overall == "verified" );
    REQUIRE( v.issues.empty() );
    // Canonical size: the mid-file CRLF collapsed, the lone CR kept.
    REQUIRE( v.verifiedBytes == static_cast<std::int64_t>( raw.size() - 1 ) );
}

TEST_CASE( "authority PV-B15: digest drift is a named hard rejection at PackVerifier itself",
           "[teaching_r4][boundary][authority][assets]" )
{
    // Same canonical LENGTH, different bytes — isolates the digest check from
    // the size check that runs first for committed fixtures.
    DirectPack fx( QStringLiteral( "data/drifted.bin" ), "current-bytes", "current-bytez" );
    const sicnu::labpack::PackVerification v = fx.verify();
    REQUIRE( v.overall == "failed" );
    REQUIRE( pvHasIssue( v, "lab.pack_checksum_mismatch", "drifted.bin" ) );
}

TEST_CASE( "authority PV-B06: a missing committed fixture is a typed hard rejection",
           "[teaching_r4][boundary][authority][assets]" )
{
    DirectPack fx( QStringLiteral( "data/absent.tif" ), "soon-gone", "soon-gone" );
    REQUIRE( QFile::remove( fx.root.filePath( QStringLiteral( "data/absent.tif" ) ) ) );
    const sicnu::labpack::PackVerification v = fx.verify();
    REQUIRE( v.overall == "failed" );
    REQUIRE( pvHasIssue( v, "lab.pack_input_missing", "absent.tif" ) );
}

// ------------------------- EDIT D3 (header comment) ------------------------
// In the header comment block, after the line
//     sicnu::teaching_admin::canonicalFileSha256/Size — direct unit pins
// insert:
//     sicnu::labpack::PackVerifier::verify — the authority direct surface
//     (round-2 遗留3: the same EOL/encoding classes at PackVerifier itself)
// and update the tail note "(CHUNK-1/2/3) exercise the streaming pending-CR
// logic directly — the inventory fixtures are tiny, so these are the only
// cases > 8000 bytes." to drop the stale 8000-byte rationale (the mirror's
// head is gone; CHUNK fixtures are aligned to the authority's 64KiB reads).
