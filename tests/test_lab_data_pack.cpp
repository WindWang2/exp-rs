/***************************************************************************
  tests/test_lab_data_pack.cpp — sicnu.lab-pack/1 contract: load + verify.

  Independent oracle: expected checksums are computed in the TEST with
  QCryptographicHash over bytes the test itself wrote, then the pack is
  built as JSON text and verified against disk. Nothing here reuses the
  implementation's hashing path to decide pass/fail — the pack's verifier
  must agree with an independently computed digest or fail loudly.
  Negative classes: corrupt byte, missing file, wrong size, bad schema,
  duplicate path, missing fixture hash, unicode path, no-write guarantee.
 ***************************************************************************/

#include "agent/lab_data_pack.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using sicnu::agent::LabDataPack;
using sicnu::agent::LabDataPackResult;
using sicnu::agent::LabPackInput;
using sicnu::agent::LabPackVerifier;
using sicnu::agent::LabPackVerification;

namespace
{

QString sha256Of( const QByteArray &bytes )
{
  return QString::fromLatin1(
    QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

/// Writes @p relative under @p root (parents created) with @p bytes.
void writeFile( const QString &root, const QString &relative, const QByteArray &bytes )
{
  const QString absolute = QDir( root ).filePath( relative );
  REQUIRE( QDir().mkpath( QFileInfo( absolute ).absolutePath() ) );
  QFile file( absolute );
  REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  REQUIRE( file.write( bytes ) == bytes.size() );
}

/// Builds a minimal valid pack JSON string around @p inputEntries.
QString packJson( const QString &labId, const QString &inputEntries )
{
  return QStringLiteral( R"({
  "schema_version": "sicnu.lab-pack/1",
  "lab_id": "%1",
  "pack_version": "1.0",
  "license": "generated-in-repo",
  "inputs": [%2]
})" )
    .arg( labId, inputEntries );
}

QString fixtureInputEntry( const QString &path, const QString &sha, qint64 bytes )
{
  return QStringLiteral(
           R"({
    "path": "%1",
    "role": "fixture",
    "provenance": "committed-fixture",
    "sha256": "%2",
    "bytes": %3,
    "sensor_truth": "synthetic, band 1 only"
  })" )
    .arg( path, sha )
    .arg( bytes );
}

} // namespace

TEST_CASE( "lab pack: committed fixture verifies against independent digest",
           "[lab_pack][known_answer]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QByteArray payload = QByteArray( "deterministic lab fixture bytes \x01\x02", 32 );
  writeFile( dir.path(), "tests/fixtures/lab/reference.tif", payload );
  const QString digest = sha256Of( payload );

  const QString packPath = QDir( dir.path() ).filePath( "lab_reference.pack.json" );
  writeFile( dir.path(), "lab_reference.pack.json",
             packJson( "lab_reference",
                       fixtureInputEntry( "tests/fixtures/lab/reference.tif", digest,
                                          payload.size() ) )
               .toUtf8() );

  const auto loaded = LabDataPack::load( packPath );
  INFO( "load: " << loaded.errorCode().toStdString() << " / "
                 << loaded.errorMessage().toStdString() );
  REQUIRE( loaded );
  REQUIRE( loaded.pack().labId == QLatin1String( "lab_reference" ) );
  REQUIRE( loaded.pack().inputs.size() == 1 );

  const LabPackVerification verification = LabPackVerifier::verify( loaded.pack(), dir.path() );
  REQUIRE( verification.overall == QLatin1String( "verified" ) );
  REQUIRE( verification.issues.isEmpty() );
  REQUIRE( verification.verifiedBytes == payload.size() );

  // The summary is deterministic and carries the typed evidence.
  const Json::Value summary = verification.toJson();
  REQUIRE( summary["overall"].asString() == "verified" );
  REQUIRE( summary["issues"].isArray() );
  REQUIRE( summary["issues"].empty() );
}

TEST_CASE( "lab pack: corrupt fixture byte fails with checksum mismatch",
           "[lab_pack][negative]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  QByteArray payload( 128, 'A' );
  writeFile( dir.path(), "fixtures/a.tif", payload );
  const QString declaredDigest = sha256Of( payload );

  // Flip one byte AFTER the digest was computed.
  payload[7] = 'B';
  writeFile( dir.path(), "fixtures/a.tif", payload );

  const QString packPath = QDir( dir.path() ).filePath( "p.pack.json" );
  writeFile( dir.path(), "p.pack.json",
             packJson( "p", fixtureInputEntry( "fixtures/a.tif", declaredDigest, 128 ) )
               .toUtf8() );

  const auto loaded = LabDataPack::load( packPath );
  REQUIRE( loaded );
  const LabPackVerification verification = LabPackVerifier::verify( loaded.pack(), dir.path() );
  REQUIRE( verification.overall == QLatin1String( "failed" ) );
  REQUIRE( verification.issues.size() == 1 );
  REQUIRE( verification.issues.first()["code"].asString() == "lab.pack_checksum_mismatch" );
  REQUIRE( verification.issues.first()["declared_sha256"].asString()
           == declaredDigest.toStdString() );
}

TEST_CASE( "lab pack: missing committed fixture fails, missing generated input degrades",
           "[lab_pack][negative]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString packPath = QDir( dir.path() ).filePath( "p.pack.json" );
  const QString committedEntry =
    fixtureInputEntry( "fixtures/absent.tif", sha256Of( QByteArray( "x" ) ), 1 );
  const QString generatedEntry = QStringLiteral(
    R"({
    "path": "data/samples/landsat_sample.tif",
    "role": "sample",
    "provenance": "generated-samples",
    "generator": "sicnu_generate_samples data/samples",
    "bytes": 999
  })" );
  writeFile( dir.path(), "p.pack.json",
             packJson( "p", committedEntry + "," + generatedEntry ).toUtf8() );

  const auto loaded = LabDataPack::load( packPath );
  REQUIRE( loaded );
  const LabPackVerification verification = LabPackVerifier::verify( loaded.pack(), dir.path() );
  REQUIRE( verification.overall == QLatin1String( "failed" ) );
  REQUIRE( verification.issues.size() == 2 );
  REQUIRE( verification.issues.first()["code"].asString() == "lab.pack_input_missing" );
  REQUIRE( verification.issues.first()["provenance"].asString() == "committed-fixture" );
  REQUIRE( verification.issues.at( 1 )["provenance"].asString() == "generated-samples" );
}

TEST_CASE( "lab pack: regenerable size drift degrades but never fails",
           "[lab_pack][negative]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  writeFile( dir.path(), "data/samples/landsat_sample.tif", QByteArray( 10, 'z' ) );
  const QString generatedEntry = QStringLiteral(
    R"({
    "path": "data/samples/landsat_sample.tif",
    "role": "sample",
    "provenance": "generated-samples",
    "bytes": 999999
  })" );
  const QString packPath = QDir( dir.path() ).filePath( "p.pack.json" );
  writeFile( dir.path(), "p.pack.json", packJson( "p", generatedEntry ).toUtf8() );

  const auto loaded = LabDataPack::load( packPath );
  REQUIRE( loaded );
  const LabPackVerification verification = LabPackVerifier::verify( loaded.pack(), dir.path() );
  REQUIRE( verification.overall == QLatin1String( "degraded" ) );
  REQUIRE( verification.issues.size() == 1 );
  REQUIRE( verification.issues.first()["code"].asString() == "lab.pack_regenerated_size_drift" );
}

TEST_CASE( "lab pack: typed load failures", "[lab_pack][negative]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  SECTION( "unreadable" )
  {
    const auto result = LabDataPack::load( QDir( dir.path() ).filePath( "absent.pack.json" ) );
    REQUIRE( !result );
    REQUIRE( result.errorCode() == QLatin1String( "lab.pack_unreadable" ) );
  }

  SECTION( "wrong schema" )
  {
    const QString path = QDir( dir.path() ).filePath( "s.pack.json" );
    writeFile( dir.path(), "s.pack.json", QByteArray( "{\"schema_version\": \"nope/0\"}" ) );
    const auto result = LabDataPack::load( path );
    REQUIRE( !result );
    REQUIRE( result.errorCode() == QLatin1String( "lab.pack_schema" ) );
  }

  SECTION( "duplicate input path" )
  {
    const QString entry = fixtureInputEntry( "a.tif", sha256Of( QByteArray( "x" ) ), 1 );
    const QString path = QDir( dir.path() ).filePath( "d.pack.json" );
    writeFile( dir.path(), "d.pack.json", packJson( "d", entry + "," + entry ).toUtf8() );
    const auto result = LabDataPack::load( path );
    REQUIRE( !result );
    REQUIRE( result.errorCode() == QLatin1String( "lab.pack_input" ) );
  }

  SECTION( "committed fixture without sha256" )
  {
    const QString entry = QStringLiteral(
      R"({"path": "a.tif", "role": "fixture", "provenance": "committed-fixture", "bytes": 1})" );
    const QString path = QDir( dir.path() ).filePath( "n.pack.json" );
    writeFile( dir.path(), "n.pack.json", packJson( "n", entry ).toUtf8() );
    const auto result = LabDataPack::load( path );
    REQUIRE( !result );
    REQUIRE( result.errorCode() == QLatin1String( "lab.pack_input" ) );
  }

  SECTION( "malformed sha256" )
  {
    const QString entry = fixtureInputEntry( "a.tif", "NOTAHASH", 1 );
    const QString path = QDir( dir.path() ).filePath( "m.pack.json" );
    writeFile( dir.path(), "m.pack.json", packJson( "m", entry ).toUtf8() );
    const auto result = LabDataPack::load( path );
    REQUIRE( !result );
    REQUIRE( result.errorCode() == QLatin1String( "lab.pack_input" ) );
  }
}

TEST_CASE( "lab pack: unicode paths verify and verification never writes",
           "[lab_pack][unicode]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QByteArray payload = "syllabus \xe5\xae\x9e\xe9\xaa\x8c\xe6\x95\xb0\xe6\x8d\xae";
  writeFile( dir.path(), QStringLiteral( "\xe6\x95\xb0\xe6\x8d\xae/\xe5\xae\x9e\xe9\xaa\x8c.tif" ),
             payload );

  const QString entry =
    fixtureInputEntry( QStringLiteral( "\xe6\x95\xb0\xe6\x8d\xae/\xe5\xae\x9e\xe9\xaa\x8c.tif" ),
                       sha256Of( payload ), payload.size() );
  const QString packPath = QDir( dir.path() ).filePath( "u.pack.json" );
  writeFile( dir.path(), "u.pack.json", packJson( "u", entry ).toUtf8() );

  const auto loaded = LabDataPack::load( packPath );
  REQUIRE( loaded );

  // Snapshot the directory tree before/after: verify must not write anything.
  const QDir tree( dir.path() );
  const auto before = tree.entryInfoList( QDir::AllEntries | QDir::Hidden );
  const LabPackVerification verification = LabPackVerifier::verify( loaded.pack(), dir.path() );
  const auto after = tree.entryInfoList( QDir::AllEntries | QDir::Hidden );
  REQUIRE( verification.overall == QLatin1String( "verified" ) );
  REQUIRE( before.size() == after.size() );
}

TEST_CASE( "lab pack: loadPacksFromDir sorts by name and reports problems",
           "[lab_pack][contract]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString good = fixtureInputEntry( "a.tif", sha256Of( QByteArray( "x" ) ), 1 );
  writeFile( dir.path(), "b_second.pack.json", packJson( "b_second", good ).toUtf8() );
  writeFile( dir.path(), "a_first.pack.json", packJson( "a_first", good ).toUtf8() );
  writeFile( dir.path(), "c_broken.pack.json", QByteArray( "{ not json" ) );

  QVector<LabDataPackResult> problems;
  const auto packs = LabPackVerifier::loadPacksFromDir( dir.path(), &problems );
  REQUIRE( packs.size() == 2 );
  REQUIRE( packs.first().labId == QLatin1String( "a_first" ) );
  REQUIRE( packs.at( 1 ).labId == QLatin1String( "b_second" ) );
  REQUIRE( problems.size() == 1 );
  REQUIRE( problems.first().errorCode() == QLatin1String( "lab.pack_schema" ) );
}

// ---------------------------------------------------------------------------
// Committed-pack drift guards (source tree = CMAKE_SOURCE_DIR).
// ---------------------------------------------------------------------------

namespace
{
QString labsSourceRoot()
{
  return QStringLiteral( CMAKE_SOURCE_DIR );
}
} // namespace

TEST_CASE( "committed lab packs all load cleanly", "[lab_pack][drift]" )
{
  QVector<LabDataPackResult> problems;
  const auto packs =
    LabPackVerifier::loadPacksFromDir( labsSourceRoot() + "/data/labs/packs", &problems );
  REQUIRE( problems.isEmpty() );
  REQUIRE( packs.size() >= 16 ); // 16 lab ids + grading_corpus
}

namespace
{
/// data/labs/lab-registry.json is the authority for lab identity: a lab may be
/// known under a legacy id (the D3 `sar_processing` vocabulary) and its pack
/// may be filed under that legacy name. Both sides are canonicalised here so
/// the parity assertion compares canonical ids on both sides. Failing to
/// resolve aliases made this gate red the moment a canonical lab was added.
QString canonicalizeLabId( const QString &id, const QJsonObject &registry )
{
  const QJsonObject canonical = registry.value( "canonical" ).toObject();
  for ( auto it = canonical.constBegin(); it != canonical.constEnd(); ++it )
  {
    const QJsonArray aliases = it.value().toObject().value( "aliases" ).toArray();
    for ( const QJsonValue &alias : aliases )
    {
      if ( alias.toString() == id )
        return it.key();
    }
  }
  return id;
}

QJsonObject loadLabRegistry()
{
  QFile file( labsSourceRoot() + "/data/labs/lab-registry.json" );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
  REQUIRE( doc.isObject() );
  return doc.object();
}
} // namespace

TEST_CASE( "every lab has a pack; every pack names a known lab", "[lab_pack][drift]" )
{
  const QJsonObject registry = loadLabRegistry();
  const QJsonObject aliasPacks = registry.value( "alias_packs" ).toObject();
  const QJsonObject nonLabPacks = registry.value( "non_lab_packs" ).toObject();

  QDir labsDir( labsSourceRoot() + "/data/labs" );
  QSet<QString> labIds;
  for ( const QFileInfo &entry :
        labsDir.entryInfoList( QStringList() << "*.lab.json" << "*.labspec.json", QDir::Files ) )
  {
    QFile file( entry.absoluteFilePath() );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    // lab ids come from the document; both formats carry "id".
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
    REQUIRE( doc.isObject() );
    labIds.insert( canonicalizeLabId( doc.object().value( "id" ).toString(), registry ) );
  }
  REQUIRE( !labIds.isEmpty() );

  QDir packsDir( labsSourceRoot() + "/data/labs/packs" );
  QSet<QString> packIds;
  for ( const QFileInfo &entry :
        packsDir.entryInfoList( QStringList() << "*.pack.json", QDir::Files ) )
  {
    const QString base = entry.baseName();
    if ( nonLabPacks.contains( base ) )
      continue; // declared deployment unit, not a lab (e.g. grading_corpus)
    const QJsonValue target = aliasPacks.value( base );
    packIds.insert( target.isString() ? target.toString() : base );
  }
  REQUIRE( packIds == labIds );
}

TEST_CASE( "committed fixtures match pack checksums (corpus verifies; no pack fails)",
           "[lab_pack][drift]" )
{
  QVector<LabDataPackResult> problems;
  const auto packs =
    LabPackVerifier::loadPacksFromDir( labsSourceRoot() + "/data/labs/packs", &problems );
  REQUIRE( problems.isEmpty() );

  bool sawCorpus = false;
  for ( const LabDataPack &pack : packs )
  {
    const LabPackVerification verification =
      LabPackVerifier::verify( pack, labsSourceRoot() );
    INFO( pack.labId.toStdString() << " -> " << verification.overall.toStdString() );
    // In the source tree regenerable inputs are absent by design (data is
    // gitignored); that degrades but must never fail. A failure here means a
    // committed fixture was edited without regenerating its pack.
    REQUIRE( verification.overall != QLatin1String( "failed" ) );
    if ( pack.labId == QLatin1String( "grading_corpus" ) )
    {
      sawCorpus = true;
      REQUIRE( verification.overall == QLatin1String( "verified" ) );
      REQUIRE( verification.issues.isEmpty() );
    }
  }
  REQUIRE( sawCorpus );
}

TEST_CASE( "pack manifests are in sync with gen_lab_packs.py (zero diff)",
           "[lab_pack][drift][docs]" )
{
  QProcess gen;
  gen.setWorkingDirectory( labsSourceRoot() );
  gen.setArguments( { labsSourceRoot() + QStringLiteral( "/scripts/gen_lab_packs.py" ),
                      QStringLiteral( "--check" ) } );
  // python3 first (Linux/CI convention), then the Windows launcher name.
  bool started = false;
  for ( const QString &program : { QStringLiteral( "python3" ), QStringLiteral( "python" ) } )
  {
    gen.setProgram( program );
    gen.start();
    if ( gen.waitForStarted( 10000 ) )
    {
      started = true;
      break;
    }
  }
  REQUIRE( started ); // no silent skips: a host without python cannot run this gate
  REQUIRE( gen.waitForFinished( 120000 ) );
  const QByteArray out = gen.readAllStandardOutput() + gen.readAllStandardError();
  INFO( out.toStdString() );
  REQUIRE( gen.exitCode() == 0 );
}
