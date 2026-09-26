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

  The #1336 first-round semantics (fileSha256 canonicalization) must NOT
  regress: B01/B02/B17 pin it from the outside. Chunk-boundary cases
  (CHUNK-1/2/3) exercise the streaming pending-CR logic directly — the
  inventory fixtures are tiny, so these are the only cases > 8000 bytes.
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
    // 8000-byte text head, then 64KiB chunk whose LAST byte is '\r' with its
    // '\n' in the next chunk, then more text, then a lone trailing '\r' at
    // EOF (kept as-is).
    QByteArray raw;
    raw.reserve( 8000 + 65536 + 16 + 1 );
    raw.append( QByteArray( 8000, 'h' ) );
    QByteArray block( 65536, 'a' );
    block[65535] = '\r'; // last byte of the first 64KiB chunk after head
    raw.append( block );
    raw.append( "\nmid" ); // '\n' arrives in the NEXT chunk
    raw.append( QByteArray( 8, 'b' ) );
    raw.append( '\r' ); // lone CR at EOF — kept

    const QString path = root.filePath( QStringLiteral( "chunked.txt" ) );
    writeBytes( path, raw );

    // The oracle normalizes the WHOLE content in one pass (no chunking);
    // equality proves the streamed pending-CR logic is equivalent.
    CHECK( canonicalFileSha256( path ) == oracleSha256( canonicalBytes( raw ) ) );
    CHECK( canonicalFileSize( path ) == static_cast<qint64>( canonicalBytes( raw ).size() ) );
    // The mid-file CRLF normalized away, the trailing lone CR kept:
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
