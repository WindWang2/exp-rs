/***************************************************************************
  tests/test_lab_data_pack.cpp — sicnu.lab-pack/1 contract: load + verify.

  Qt-free leaf test over sicnu::labpack (src/lab_pack) — the ONE pack
  parser/validator shared by the agent façade and the teacher console.

  Independent oracle: expected checksums are HARDCODED SHA-256 constants
  (NIST FIPS 180-4 values computed outside this repo's hashing paths), then
  the pack is built as JSON text and verified against disk. Nothing here
  reuses the implementation's hashing path to decide pass/fail — the pack's
  verifier must agree with an independently computed digest or fail loudly.
  Negative classes: corrupt byte, missing file, wrong size, bad schema,
  duplicate path, missing fixture hash, unicode path, no-write guarantee.
 ***************************************************************************/

#include "lab_pack/lab_pack.h"

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>
#include <QJsonValue>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sicnu::labpack::PackDocument;
using sicnu::labpack::PackInput;
using sicnu::labpack::PackLoadResult;
using sicnu::labpack::PackVerification;
using sicnu::labpack::PackVerifier;

namespace
{

/// RAII temp directory (Qt-free twin of QTemporaryDir).
struct TempDir
{
  fs::path path;
  TempDir()
  {
    std::error_code ec;
    const fs::path base = fs::temp_directory_path( ec );
    for ( int attempt = 0; attempt < 64 && path.empty(); ++attempt )
    {
      const fs::path candidate =
        base / ( "lab_pack_test_" + std::to_string( ::rand() ) + std::to_string( attempt ) );
      if ( fs::create_directories( candidate, ec ) )
        path = candidate;
    }
    REQUIRE( !path.empty() );
  }
  ~TempDir()
  {
    if ( !path.empty() )
    {
      std::error_code ec;
      fs::remove_all( path, ec );
    }
  }
  TempDir( const TempDir & ) = delete;
  TempDir &operator=( const TempDir & ) = delete;
  std::string u8() const { return sicnu::labpack::utf8FromPath( path ); }
};

/// HARDCODED independent digests — computed with an external SHA-256 tool,
/// never via the implementation under test.
constexpr const char *kSha256Fixture32Bytes =
  "9d175948f49ec28b261ea4e78728cbd879eff4f888824201b8b53a64ee714dc6"; // "deterministic lab fixture bytes " (32 bytes)
constexpr const char *kSha256A128 =
  "b6ac3cc10386331c765f04f041c147d0f278f2aed8eaa021e2d0057fc6f6ff9e"; // 'A' * 128
constexpr const char *kSha256X =
  "2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881"; // "x"
constexpr const char *kSha256Syllabus =
  "20d4562559a2ea41adfd6362983ad3813daae6cd53833d4c65e8eeeed880f360"; // "syllabus 实验" (UTF-8)

/// Writes @p relative (UTF-8, '/' separators) under @p root (parents created).
void writeFile( const fs::path &root, const std::string &relative, const std::string &bytes )
{
  const fs::path absolute = root / sicnu::labpack::pathFromUtf8( relative );
  std::error_code ec;
  fs::create_directories( absolute.parent_path(), ec );
  std::ofstream out( absolute, std::ios::binary | std::ios::trunc );
  REQUIRE( out.is_open() );
  out.write( bytes.data(), static_cast<std::streamsize>( bytes.size() ) );
  REQUIRE( out.good() );
}

/// Builds a minimal valid pack JSON string around @p inputEntries.
std::string packJson( const std::string &labId, const std::string &inputEntries )
{
  return std::string( R"({
  "schema_version": "sicnu.lab-pack/1",
  "lab_id": ")") + labId + R"(",
  "pack_version": "1.0",
  "license": "generated-in-repo",
  "inputs": [)" + inputEntries + R"(]
})";
}

std::string fixtureInputEntry( const std::string &path, const std::string &sha, std::int64_t bytes )
{
  return std::string( R"({
    "path": ")") + path + R"(",
    "role": "fixture",
    "provenance": "committed-fixture",
    "sha256": ")" + sha + R"(",
    "bytes": )" + std::to_string( bytes ) + R"(,
    "sensor_truth": "synthetic, band 1 only"
  })";
}

std::string slurp( const fs::path &p )
{
  std::ifstream in( p, std::ios::binary );
  return std::string( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
}

std::size_t treeEntryCount( const fs::path &dir )
{
  std::size_t count = 0;
  std::error_code ec;
  for ( fs::recursive_directory_iterator it( dir, fs::directory_options::skip_permission_denied, ec ),
        end;
        !ec && it != end; it.increment( ec ) )
    ++count;
  return count;
}

std::string sourceRoot()
{
  return CMAKE_SOURCE_DIR;
}

} // namespace

TEST_CASE( "lab pack: committed fixture verifies against independent digest",
           "[lab_pack][known_answer]" )
{
  TempDir dir;
  const std::string payload( "deterministic lab fixture bytes ", 32 );
  REQUIRE( payload.size() == 32 );
  writeFile( dir.path, "tests/fixtures/lab/reference.tif", payload );

  const std::string packBytes =
    packJson( "lab_reference",
              fixtureInputEntry( "tests/fixtures/lab/reference.tif", kSha256Fixture32Bytes,
                                 static_cast<std::int64_t>( payload.size() ) ) );
  writeFile( dir.path, "lab_reference.pack.json", packBytes );

  const PackLoadResult loaded =
    PackVerifier::load( dir.path / "lab_reference.pack.json" );
  INFO( "load: " << loaded.errorCode << " / " << loaded.errorMessage );
  REQUIRE( loaded.ok );
  REQUIRE( loaded.pack.labId == "lab_reference" );
  REQUIRE( loaded.pack.inputs.size() == 1 );

  const PackVerification verification = PackVerifier::verify( loaded.pack, dir.path );
  REQUIRE( verification.overall == "verified" );
  REQUIRE( verification.issues.empty() );
  REQUIRE( verification.verifiedBytes == static_cast<std::int64_t>( payload.size() ) );

  // The summary is deterministic and carries the typed evidence.
  const Json::Value summary = verification.toJson();
  REQUIRE( summary["overall"].asString() == "verified" );
  REQUIRE( summary["issues"].isArray() );
  REQUIRE( summary["issues"].empty() );
}

TEST_CASE( "lab pack: corrupt fixture byte fails with checksum mismatch",
           "[lab_pack][negative]" )
{
  TempDir dir;
  std::string payload( 128, 'A' );
  payload[7] = 'B';
  // Declared digest stays the PRISTINE bytes (hardcoded, independent).
  writeFile( dir.path, "fixtures/a.tif", payload );

  const std::string packBytes =
    packJson( "p", fixtureInputEntry( "fixtures/a.tif", kSha256A128, 128 ) );
  writeFile( dir.path, "p.pack.json", packBytes );

  const PackLoadResult loaded = PackVerifier::load( dir.path / "p.pack.json" );
  REQUIRE( loaded.ok );
  const PackVerification verification = PackVerifier::verify( loaded.pack, dir.path );
  REQUIRE( verification.overall == "failed" );
  REQUIRE( verification.issues.size() == 1 );
  REQUIRE( verification.issues.at( 0 )["code"].asString() == "lab.pack_checksum_mismatch" );
  REQUIRE( verification.issues.at( 0 )["declared_sha256"].asString() == kSha256A128 );
  REQUIRE( verification.issues.at( 0 )["actual_sha256"].asString() != kSha256A128 );
}

TEST_CASE( "lab pack: missing committed fixture fails, missing generated input degrades",
           "[lab_pack][negative]" )
{
  TempDir dir;
  const std::string committedEntry =
    fixtureInputEntry( "fixtures/absent.tif", kSha256X, 1 );
  const std::string generatedEntry = std::string( R"({
    "path": "data/samples/landsat_sample.tif",
    "role": "sample",
    "provenance": "generated-samples",
    "generator": "sicnu_generate_samples data/samples",
    "bytes": 999
  })" );
  writeFile( dir.path, "p.pack.json", packJson( "p", committedEntry + "," + generatedEntry ) );

  const PackLoadResult loaded = PackVerifier::load( dir.path / "p.pack.json" );
  REQUIRE( loaded.ok );
  const PackVerification verification = PackVerifier::verify( loaded.pack, dir.path );
  REQUIRE( verification.overall == "failed" );
  REQUIRE( verification.issues.size() == 2 );
  REQUIRE( verification.issues.at( 0 )["code"].asString() == "lab.pack_input_missing" );
  REQUIRE( verification.issues.at( 0 )["provenance"].asString() == "committed-fixture" );
  REQUIRE( verification.issues.at( 1 )["provenance"].asString() == "generated-samples" );
}

TEST_CASE( "lab pack: regenerable size drift degrades but never fails",
           "[lab_pack][negative]" )
{
  TempDir dir;
  writeFile( dir.path, "data/samples/landsat_sample.tif", std::string( 10, 'z' ) );
  const std::string generatedEntry = std::string( R"({
    "path": "data/samples/landsat_sample.tif",
    "role": "sample",
    "provenance": "generated-samples",
    "bytes": 999999
  })" );
  writeFile( dir.path, "p.pack.json", packJson( "p", generatedEntry ) );

  const PackLoadResult loaded = PackVerifier::load( dir.path / "p.pack.json" );
  REQUIRE( loaded.ok );
  const PackVerification verification = PackVerifier::verify( loaded.pack, dir.path );
  REQUIRE( verification.overall == "degraded" );
  REQUIRE( verification.issues.size() == 1 );
  REQUIRE( verification.issues.at( 0 )["code"].asString() == "lab.pack_regenerated_size_drift" );
}

TEST_CASE( "lab pack: typed load failures", "[lab_pack][negative]" )
{
  TempDir dir;

  SECTION( "unreadable" )
  {
    const PackLoadResult result = PackVerifier::load( dir.path / "absent.pack.json" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_unreadable" );
  }

  SECTION( "wrong schema" )
  {
    writeFile( dir.path, "s.pack.json", std::string( "{\"schema_version\": \"nope/0\"}" ) );
    const PackLoadResult result = PackVerifier::load( dir.path / "s.pack.json" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_schema" );
  }

  SECTION( "duplicate input path" )
  {
    const std::string entry = fixtureInputEntry( "a.tif", kSha256X, 1 );
    writeFile( dir.path, "d.pack.json", packJson( "d", entry + "," + entry ) );
    const PackLoadResult result = PackVerifier::load( dir.path / "d.pack.json" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_input" );
  }

  SECTION( "committed fixture without sha256" )
  {
    const std::string entry = std::string(
      R"({"path": "a.tif", "role": "fixture", "provenance": "committed-fixture", "bytes": 1})" );
    writeFile( dir.path, "n.pack.json", packJson( "n", entry ) );
    const PackLoadResult result = PackVerifier::load( dir.path / "n.pack.json" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_input" );
  }

  SECTION( "malformed sha256" )
  {
    const std::string entry = fixtureInputEntry( "a.tif", "NOTAHASH", 1 );
    writeFile( dir.path, "m.pack.json", packJson( "m", entry ) );
    const PackLoadResult result = PackVerifier::load( dir.path / "m.pack.json" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_input" );
  }

  SECTION( "UPPERCASE sha256 is rejected — the pack contract pins lowercase hex" )
  {
    std::string upper( kSha256A128 );
    std::transform( upper.begin(), upper.end(), upper.begin(),
                    []( unsigned char c ) { return static_cast<char>( ::toupper( c ) ); } );
    const std::string entry = fixtureInputEntry( "a.tif", upper, 128 );
    writeFile( dir.path, "u.pack.json", packJson( "u", entry ) );
    const PackLoadResult result = PackVerifier::load( dir.path / "u.pack.json" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_input" );
  }

  SECTION( "missing license is a pack_field failure" )
  {
    const std::string entry = fixtureInputEntry( "a.tif", kSha256X, 1 );
    const std::string noLicense = std::string( R"({
  "schema_version": "sicnu.lab-pack/1",
  "lab_id": "nolicense",
  "pack_version": "1.0",
  "inputs": [)" ) + entry + R"(]
})";
    writeFile( dir.path, "l.pack.json", noLicense );
    const PackLoadResult result = PackVerifier::load( dir.path / "l.pack.json" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_field" );
  }

  SECTION( "empty inputs array is a pack_field failure" )
  {
    const std::string emptyInputs = std::string( R"({
  "schema_version": "sicnu.lab-pack/1",
  "lab_id": "empty",
  "pack_version": "1.0",
  "license": "x",
  "inputs": []
})" );
    writeFile( dir.path, "e.pack.json", emptyInputs );
    const PackLoadResult result = PackVerifier::load( dir.path / "e.pack.json" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_field" );
  }
}

TEST_CASE( "lab pack: loadFromBytes and load are the same parser truth",
           "[lab_pack][contract]" )
{
  TempDir dir;
  const std::string entry = fixtureInputEntry( "a.tif", kSha256X, 1 );
  const std::string bytes = packJson( "bytes_lab", entry );
  const PackLoadResult fromBytes = PackVerifier::loadFromBytes( bytes, "inline" );
  REQUIRE( fromBytes.ok );
  REQUIRE( fromBytes.pack.labId == "bytes_lab" );
  writeFile( dir.path, "same.pack.json", bytes );
  const PackLoadResult fromFile = PackVerifier::load( dir.path / "same.pack.json" );
  REQUIRE( fromFile.ok );
  // Identical documents parse to identical pack documents either way.
  REQUIRE( fromBytes.pack.labId == fromFile.pack.labId );
  REQUIRE( fromBytes.pack.packVersion == fromFile.pack.packVersion );
  REQUIRE( fromBytes.pack.license == fromFile.pack.license );
  REQUIRE( fromBytes.pack.inputs.size() == fromFile.pack.inputs.size() );
  REQUIRE( fromBytes.pack.inputs.size() == 1 );
  REQUIRE( fromBytes.pack.inputs.at( 0 ).path == fromFile.pack.inputs.at( 0 ).path );
  REQUIRE( fromBytes.pack.inputs.at( 0 ).provenance
           == sicnu::labpack::Provenance::CommittedFixture );
}

TEST_CASE( "lab pack: unicode paths verify and verification never writes",
           "[lab_pack][unicode]" )
{
  TempDir dir;
  const std::string payload = "syllabus \xe5\xae\x9e\xe9\xaa\x8c\xe6\x95\xb0\xe6\x8d\xae";
  writeFile( dir.path, "\xe6\x95\xb0\xe6\x8d\xae/\xe5\xae\x9e\xe9\xaa\x8c.tif", payload );

  const std::string entry =
    fixtureInputEntry( "\xe6\x95\xb0\xe6\x8d\xae/\xe5\xae\x9e\xe9\xaa\x8c.tif", kSha256Syllabus,
                       static_cast<std::int64_t>( payload.size() ) );
  writeFile( dir.path, "u.pack.json", packJson( "u", entry ) );

  const PackLoadResult loaded = PackVerifier::load( dir.path / "u.pack.json" );
  REQUIRE( loaded.ok );

  // Snapshot the directory tree before/after: verify must not write anything.
  const std::size_t before = treeEntryCount( dir.path );
  const PackVerification verification = PackVerifier::verify( loaded.pack, dir.path );
  const std::size_t after = treeEntryCount( dir.path );
  REQUIRE( verification.overall == "verified" );
  REQUIRE( before == after );
}

TEST_CASE( "lab pack: loadPacksFromDir sorts by name and reports problems",
           "[lab_pack][contract]" )
{
  TempDir dir;
  const std::string good = fixtureInputEntry( "a.tif", kSha256X, 1 );
  writeFile( dir.path, "b_second.pack.json", packJson( "b_second", good ) );
  writeFile( dir.path, "a_first.pack.json", packJson( "a_first", good ) );
  writeFile( dir.path, "c_broken.pack.json", std::string( "{ not json" ) );

  std::vector<PackLoadResult> problems;
  const std::vector<PackDocument> packs = PackVerifier::loadPacksFromDir( dir.path, &problems );
  REQUIRE( packs.size() == 2 );
  REQUIRE( packs.at( 0 ).labId == "a_first" );
  REQUIRE( packs.at( 1 ).labId == "b_second" );
  REQUIRE( problems.size() == 1 );
  REQUIRE( problems.at( 0 ).errorCode == "lab.pack_schema" );
}

TEST_CASE( "lab pack: pack-relative inputs must stay under the verification root",
           "[lab_pack][negative]" )
{
  TempDir dir;
  writeFile( dir.path, "in_root.tif", std::string( 4, 'G' ) );
  // absolute + ../ lexical escapes
  const std::string absoluteEntry =
    fixtureInputEntry( "/etc/passwd", kSha256X, 1 );
  const std::string dotDotEntry = fixtureInputEntry( "../outside.tif", kSha256X, 1 );
  writeFile( dir.path, "abs.pack.json", packJson( "abs", absoluteEntry ) );
  writeFile( dir.path, "dot.pack.json", packJson( "dot", dotDotEntry ) );

  {
    const PackLoadResult loaded = PackVerifier::load( dir.path / "abs.pack.json" );
    REQUIRE( loaded.ok );
    const PackVerification v = PackVerifier::verify( loaded.pack, dir.path );
    REQUIRE( v.overall == "failed" );
    REQUIRE( v.issues.size() == 1 );
    REQUIRE( v.issues.at( 0 )["code"].asString() == "lab.pack_input_outside_root" );
  }
  {
    const PackLoadResult loaded = PackVerifier::load( dir.path / "dot.pack.json" );
    REQUIRE( loaded.ok );
    const PackVerification v = PackVerifier::verify( loaded.pack, dir.path );
    REQUIRE( v.overall == "failed" );
    REQUIRE( v.issues.size() == 1 );
    REQUIRE( v.issues.at( 0 )["code"].asString() == "lab.pack_input_outside_root" );
  }
}

// ---------------------------------------------------------------------------
// Committed-pack drift guards (source tree = CMAKE_SOURCE_DIR).
// ---------------------------------------------------------------------------

TEST_CASE( "committed lab packs all load cleanly", "[lab_pack][drift]" )
{
  std::vector<PackLoadResult> problems;
  const std::vector<PackDocument> packs = PackVerifier::loadPacksFromDir(
    sicnu::labpack::pathFromUtf8( sourceRoot() + "/data/labs/packs" ), &problems );
  REQUIRE( problems.empty() );
  REQUIRE( packs.size() >= 16 ); // 16 lab ids + grading_corpus
}

namespace
{

/// data/labs/lab-registry.json is the authority for lab identity: a lab may be
/// known under a legacy id (the D3 `sar_processing` vocabulary) and its pack
/// may be filed under that legacy name. Both sides are canonicalised here so
/// the parity assertion compares canonical ids on both sides. Failing to
/// resolve aliases made this gate red the moment a canonical lab was added.
std::string canonicalizeLabId( const std::string &id, const Json::Value &registry )
{
  const Json::Value canonical = registry["canonical"];
  for ( const auto &member : canonical.getMemberNames() )
  {
    const Json::Value aliases = canonical[member]["aliases"];
    for ( const Json::Value &alias : aliases )
      if ( alias.asString() == id )
        return member;
  }
  return id;
}

Json::Value loadLabRegistry()
{
  const std::string bytes = slurp(
    sicnu::labpack::pathFromUtf8( sourceRoot() + "/data/labs/lab-registry.json" ) );
  Json::Value root;
  Json::CharReaderBuilder builder;
  std::string errors;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  REQUIRE( reader->parse( bytes.data(), bytes.data() + bytes.size(), &root, &errors ) );
  return root;
}

} // namespace

TEST_CASE( "every lab has a pack; every pack names a known lab", "[lab_pack][drift]" )
{
  const Json::Value registry = loadLabRegistry();
  const Json::Value aliasPacks = registry["alias_packs"];
  const Json::Value nonLabPacks = registry["non_lab_packs"];

  std::vector<std::string> labIds;
  for ( const fs::path &dir :
        { sicnu::labpack::pathFromUtf8( sourceRoot() + "/data/labs" ) } )
  {
    std::error_code ec;
    for ( fs::directory_iterator it( dir, ec ), end; !ec && it != end; it.increment( ec ) )
    {
      const std::string name = sicnu::labpack::utf8FromPath( it->path() );
      const bool isLab =
        ( name.size() > 9 && name.rfind( ".lab.json" ) == name.size() - 9 )
        || ( name.size() > 13 && name.rfind( ".labspec.json" ) == name.size() - 13 );
      if ( !isLab )
        continue;
      const std::string bytes = slurp( it->path() );
      Json::Value doc;
      Json::CharReaderBuilder builder;
      std::string errors;
      std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
      REQUIRE( reader->parse( bytes.data(), bytes.data() + bytes.size(), &doc, &errors ) );
      labIds.push_back( canonicalizeLabId( doc["id"].asString(), registry ) );
    }
  }
  REQUIRE( !labIds.empty() );

  std::vector<std::string> packIds;
  {
    std::error_code ec;
    fs::directory_iterator it(
      sicnu::labpack::pathFromUtf8( sourceRoot() + "/data/labs/packs" ), ec ), end;
    for ( ; !ec && it != end; it.increment( ec ) )
    {
      const std::string name = sicnu::labpack::utf8FromPath( it->path().filename() );
      if ( name.size() <= 10 || name.rfind( ".pack.json" ) != name.size() - 10 )
        continue;
      // pack file stem: strip "<stem>.pack.json"
      const std::string stem = name.substr( 0, name.size() - 10 );
      if ( nonLabPacks.isMember( stem ) )
        continue; // declared deployment unit, not a lab (e.g. grading_corpus)
      const Json::Value target = aliasPacks[stem];
      packIds.push_back( target.isString() ? target.asString() : stem );
    }
  }
  std::sort( labIds.begin(), labIds.end() );
  labIds.erase( std::unique( labIds.begin(), labIds.end() ), labIds.end() );
  std::sort( packIds.begin(), packIds.end() );
  packIds.erase( std::unique( packIds.begin(), packIds.end() ), packIds.end() );
  REQUIRE( packIds == labIds );
}

TEST_CASE( "committed fixtures match pack checksums (corpus verifies; no pack fails)",
           "[lab_pack][drift]" )
{
  std::vector<PackLoadResult> problems;
  const std::vector<PackDocument> packs = PackVerifier::loadPacksFromDir(
    sicnu::labpack::pathFromUtf8( sourceRoot() + "/data/labs/packs" ), &problems );
  REQUIRE( problems.empty() );

  bool sawCorpus = false;
  for ( const PackDocument &pack : packs )
  {
    const PackVerification verification = PackVerifier::verify(
      pack, sicnu::labpack::pathFromUtf8( sourceRoot() ) );
    INFO( pack.labId << " -> " << verification.overall );
    // In the source tree regenerable inputs are absent by design (data is
    // gitignored); that degrades but must never fail. A failure here means a
    // committed fixture was edited without regenerating its pack.
    REQUIRE( verification.overall != "failed" );
    if ( pack.labId == "grading_corpus" )
    {
      sawCorpus = true;
      REQUIRE( verification.overall == "verified" );
      REQUIRE( verification.issues.empty() );
    }
  }
  REQUIRE( sawCorpus );
}

TEST_CASE( "pack manifests are in sync with gen_lab_packs.py (zero diff)",
           "[lab_pack][drift][docs]" )
{
  // std::system twin of the previous QProcess gate: `gen_lab_packs.py
  // --check` must exit 0 in the source tree.
  const std::string root = sourceRoot();
  REQUIRE( fs::exists( sicnu::labpack::pathFromUtf8( root + "/scripts/gen_lab_packs.py" ) ) );
  const std::string redirect =
    " > lab_pack_gen_check_stdout.txt 2>&1";
  int rc = -1;
  bool started = false;
  for ( const char *python : { "python3", "python" } )
  {
    const std::string probe = std::string( python ) + " --version" + redirect;
    if ( std::system( probe.c_str() ) == 0 )
    {
      started = true;
      const std::string cmd =
        "cd \"" + root + "\" && " + python + " scripts/gen_lab_packs.py --check" + redirect
        + " && tail -20 ${TMPDIR:-/tmp}/lab_pack_gen_check_stdout.txt >&2";
      rc = std::system( cmd.c_str() );
      break;
    }
  }
  REQUIRE( started ); // no silent skips: a host without python cannot run this gate
  INFO( "gen_lab_packs --check exit: " << rc );
  REQUIRE( rc == 0 );
}
