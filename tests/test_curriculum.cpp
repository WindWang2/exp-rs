/***************************************************************************
  tests/test_curriculum.cpp — sicnu.curriculum/1 + progress + availability.

  RS14 contract tests for the curriculum grounding seam. Everything runs as
  a small pure-C++ target (jsoncpp + std only): the catalog, the progress
  model and the probe-injected availability validator are exercised over
  fixture directories; the shipped manifest is loaded against the real repo
  data so content and contracts cannot drift apart silently.

  Oracle policy: expected issue codes come from the closed
  `sicnu.curriculum.error/1` vocabulary asserted BY CODE, and the resolver
  probes are reimplemented inline in the fixtures (a fixture labspec is
  written by the test, then expected to resolve) — nothing here trusts the
  implementation's own parsing to decide what a fixture means.
 ***************************************************************************/

#include "agent/harness/curriculum_availability.h"
#include "agent/harness/curriculum_catalog.h"
#include "agent/harness/curriculum_progress.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace std::string_literals;
using sicnu::agent::harness::buildAvailabilityReport;
using sicnu::agent::harness::CurriculumCatalog;
using sicnu::agent::harness::CurriculumIssue;
using sicnu::agent::harness::CurriculumOperatorProbes;
using sicnu::agent::harness::CurriculumPaths;
using sicnu::agent::harness::CurriculumProgress;
using sicnu::agent::harness::CurriculumProgressIssue;
using sicnu::agent::harness::resolveLabReference;
using sicnu::agent::harness::validateCurriculumManifest;

namespace
{

#ifndef SICNU_TEST_SOURCE_DIR
#define SICNU_TEST_SOURCE_DIR "."
#endif

void writeFile( const std::filesystem::path &path, const std::string &bytes )
{
  std::filesystem::create_directories( path.parent_path() );
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  REQUIRE( static_cast<bool>( out ) );
  out << bytes;
}

Json::Value parseJson( const std::string &text, std::string *error = nullptr )
{
  Json::Value root;
  Json::CharReaderBuilder builder;
  builder["collectComments"] = false;
  std::string errs;
  std::istringstream stream( text );
  const bool ok = static_cast<bool>( Json::parseFromStream( builder, stream, &root, &errs ) );
  if ( error ) *error = ok ? "" : errs;
  return root;
}

bool hasIssue( const std::vector<CurriculumIssue> &issues, const std::string &code,
               const std::string &pathFragment = {} )
{
  for ( const auto &issue : issues )
  {
    if ( issue.code != code ) continue;
    if ( !pathFragment.empty() && issue.path.find( pathFragment ) == std::string::npos ) continue;
    return true;
  }
  return false;
}

bool hasProgressIssue( const std::vector<CurriculumProgressIssue> &issues, const std::string &code )
{
  for ( const auto &issue : issues )
    if ( issue.code == code ) return true;
  return false;
}

/// A throwaway root; children are created on demand by writeFile().
class TempDir
{
  public:
    TempDir()
    {
      std::random_device device;
      std::mt19937_64 engine( device() );
      mPath = std::filesystem::temp_directory_path() /
              ( "sicnu_curriculum_test_" + std::to_string( engine() ) );
      std::filesystem::create_directories( mPath );
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all( mPath, ec ); }
    TempDir( const TempDir & ) = delete;
    TempDir &operator=( const TempDir & ) = delete;

    const std::filesystem::path &path() const { return mPath; }
    std::string str() const { return mPath.string(); }

  private:
    std::filesystem::path mPath;
};

/// Shallow labspec fixture — exactly what the resolvability probe may rely on.
std::string labSpecJson( const std::string &id, int specVersion = 2,
                         const std::string &operatorId = "rs:spectral_index" )
{
  std::string doc = "{";
  doc += "\"spec_version\": " + std::to_string( specVersion ) + ",";
  doc += "\"id\": \"" + id + "\",";
  doc += "\"title\": \"Fixture\", \"title_zh\": \"夹具\", \"objective\": \"probe\",";
  doc += "\"steps\": [{\"title\": \"s\", \"title_zh\": \"步\", \"description_zh\": \"做\",";
  doc += "\"operator_id\": \"" + operatorId + "\", \"params\": {}}]";
  doc += "}";
  return doc;
}

std::string registryJson( const std::string &canonicalId = "", const std::string &source = "",
                          const std::string &alias = "",
                          const std::string &outOfScope = "" )
{
  std::string doc = "{";
  doc += "\"schema\": \"sicnu.lab-registry/1\",";
  doc += "\"canonical\": {";
  if ( !canonicalId.empty() )
  {
    doc += "\"" + canonicalId + "\": {";
    doc += "\"course_index\": 90, \"title\": \"Fixture\", \"title_zh\": \"夹具\",";
    doc += "\"source\": \"" + source + "\"";
    if ( !alias.empty() ) doc += ", \"aliases\": [\"" + alias + "\"]";
    doc += "}";
  }
  doc += "}";
  doc += ", \"out_of_scope\": {";
  if ( !outOfScope.empty() ) doc += "\"" + outOfScope + "\": \"owned by another track\"";
  doc += "}}";
  return doc;
}

std::string packJson( const std::string &labId )
{
  std::string doc = "{";
  doc += "\"schema_version\": \"sicnu.lab-pack/1\",";
  doc += "\"lab_id\": \"" + labId + "\",";
  doc += "\"pack_version\": \"1.0\",";
  doc += "\"license\": \"generated-in-repo\",";
  doc += "\"inputs\": []}";
  return doc;
}

/// A valid one-module manifest fixture with every knob a test may want to
/// mutate afterwards via plain string replacement.
struct FixtureWorld
{
  TempDir dir;
  CurriculumPaths paths;
  const std::string labId = "lab90_probe_lab";
  const std::string otherLabId = "lab91_probe_lab";
  const std::string packName = "lab90_probe_lab";

  FixtureWorld( const std::string &operatorId = "rs:spectral_index" )
  {
    paths.labsDir = ( dir.path() / "data" / "labs" ).string();
    paths.packsDir = ( dir.path() / "data" / "labs" / "packs" ).string();
    writeFile( dir.path() / "data" / "labs" / ( labId + ".lab.json" ),
               labSpecJson( labId, 2, operatorId ) );
    writeFile( dir.path() / "data" / "labs" / ( otherLabId + ".lab.json" ),
               labSpecJson( otherLabId, 2, operatorId ) );
    writeFile( dir.path() / "data" / "labs" / "lab-registry.json", registryJson() );
    writeFile( dir.path() / "data" / "labs" / "packs" / ( packName + ".pack.json" ),
               packJson( packName ) );
  }

  std::string manifestJson( const std::string &labs = std::string() ) const
  {
    const std::string labList =
      labs.empty() ? "{\"lab_id\": \"" + labId + "\", \"role\": \"core\", "
                     "\"estimated_effort_minutes\": 120, "
                     "\"required_data_packs\": [\"" + packName + "\"]} "
                   : labs;
    return R"({
      "schema": "sicnu.curriculum/1",
      "id": "fixture_course",
      "title": "Fixture Course",
      "title_zh": "夹具课程",
      "modules": [
        {
          "id": "m01_fixture",
          "index": 1,
          "title": "Fixture Module",
          "title_zh": "夹具模块",
          "summary_zh": "用于契约测试的最小模块。",
          "learning_outcomes": ["能独立完成夹具实验"],
          "prerequisite_modules": [],
          "estimated_effort_minutes": 120,
          "labs": [)" + labList + R"(]
        }
      ]
    })";
  }

  /// Writes @p manifest and reloads a pristine catalog over the fixture.
  CurriculumCatalog load( const std::string &manifest )
  {
    writeFile( dir.path() / "data" / "curriculum" / "fixture.curriculum.json", manifest );
    CurriculumCatalog &catalog = CurriculumCatalog::instance();
    catalog.setPaths( CurriculumPaths{} );  // reset any earlier injection
    catalog.setDirectory( ( dir.path() / "data" / "curriculum" ).string() );
    catalog.setPaths( paths );
    catalog.reload();
    return catalog;  // singleton copy for reads; keep the test expressive
  }
};

CurriculumOperatorProbes fakeProbes( bool registered, bool capabilityNote )
{
  CurriculumOperatorProbes probes;
  probes.registered = [registered]( const std::string & ) { return registered; };
  probes.capabilityNote = [capabilityNote]( const std::string & ) { return capabilityNote; };
  return probes;
}

} // namespace

// ===========================================================================
// Slice A — load + schema contract
// ===========================================================================

TEST_CASE( "curriculum: ok over a fixture directory, typed unavailable without one",
           "[curriculum][load]" )
{
  SECTION( "missing directory" )
  {
    TempDir dir;
    CurriculumCatalog &catalog = CurriculumCatalog::instance();
    catalog.setPaths( CurriculumPaths{} );
    catalog.setDirectory( ( dir.path() / "does" / "not" / "exist" ).string() );
    catalog.reload();
    CHECK( catalog.status() == "unavailable" );
    CHECK( catalog.manifest().isNull() );
    CHECK( hasIssue( catalog.issues(), "manifest_unavailable" ) );
    CHECK_FALSE( catalog.loadProblems().empty() );
  }

  SECTION( "unparseable json" )
  {
    FixtureWorld world;
    auto catalog = world.load( "{ not json" );
    CHECK( catalog.status() == "unavailable" );
    CHECK( hasIssue( catalog.issues(), "manifest_unparseable" ) );
  }

  SECTION( "unknown schema version" )
  {
    FixtureWorld world;
    std::string manifest = world.manifestJson();
    const auto pos = manifest.find( "sicnu.curriculum/1" );
    REQUIRE( pos != std::string::npos );
    manifest.replace( pos, 18, "sicnu.curriculum/9" );
    auto catalog = world.load( manifest );
    CHECK( catalog.status() == "unavailable" );
    CHECK( hasIssue( catalog.issues(), "unknown_schema" ) );
  }

  SECTION( "unknown top-level key is rejected, never ignored" )
  {
    FixtureWorld world;
    std::string manifest = world.manifestJson();
    const auto pos = manifest.find( "\"modules\":" );
    REQUIRE( pos != std::string::npos );
    manifest.insert( pos, "\"bogus_key\": 1," );
    auto catalog = world.load( manifest );
    CHECK( catalog.status() == "unavailable" );
    CHECK( hasIssue( catalog.issues(), "unknown_key", "bogus_key" ) );
  }

  SECTION( "happy path" )
  {
    FixtureWorld world;
    auto catalog = world.load( world.manifestJson() );
    CHECK( catalog.status() == "ok" );
    CHECK( catalog.loaded() );
    CHECK( catalog.manifest()["id"] == "fixture_course" );
    REQUIRE( catalog.moduleIds().size() == 1 );
    CHECK( catalog.moduleIds().front() == "m01_fixture" );
    CHECK( catalog.module( "m01_fixture" )["title_zh"] == "夹具模块" );
    CHECK( catalog.module( "m99_missing" ).isNull() );
    CHECK( catalog.labRef( "m01_fixture", world.labId )["role"] == "core" );
    CHECK( catalog.labRef( "m01_fixture", "lab99_missing" ).isNull() );
  }
}

TEST_CASE( "curriculum: module ids are ordered by declared index", "[curriculum][load]" )
{
  FixtureWorld world;
  std::string manifest = world.manifestJson();
  // A second module declared AFTER m03 in the array but with a lower index.
  const std::string module2 = R"(,
        {
          "id": "m02_second",
          "index": 2,
          "title": "Second",
          "title_zh": "第二模块",
          "summary_zh": "排序夹具。",
          "learning_outcomes": [" outcome"],
          "labs": [
            {"lab_id": "lab91_probe_lab", "role": "core", "estimated_effort_minutes": 60}
          ]
        })";
  const auto pos = manifest.rfind( "]" );
  REQUIRE( pos != std::string::npos );
  manifest.insert( pos, module2 );
  auto catalog = world.load( manifest );
  REQUIRE( catalog.status() == "ok" );
  const auto ids = catalog.moduleIds();
  REQUIRE( ids.size() == 2 );
  CHECK( ids[0] == "m01_fixture" );
  CHECK( ids[1] == "m02_second" );
  REQUIRE( catalog.labIds().size() == 2 );
  CHECK( catalog.labIds()[0] == world.labId );
  CHECK( catalog.labIds()[1] == world.otherLabId );
}

// ===========================================================================
// Slice B — structure & reference contracts (typed errors)
// ===========================================================================

TEST_CASE( "curriculum: structural contracts produce typed issues", "[curriculum][validate]" )
{
  FixtureWorld world;
  const CurriculumPaths paths = world.paths;
  std::vector<CurriculumIssue> issues;
  Json::Value manifest;
  std::string error;

  auto validate = [&]( const std::string &text ) {
    issues.clear();
    manifest = parseJson( text, &error );
    REQUIRE( error.empty() );
    validateCurriculumManifest( manifest, paths, issues );
  };

  SECTION( "duplicate module id" )
  {
    std::string manifest = world.manifestJson();
    const auto pos = manifest.rfind( "]" );
    REQUIRE( pos != std::string::npos );
    manifest.insert( pos, R"(,
        {
          "id": "m01_fixture",
          "index": 2,
          "title": "Dup",
          "title_zh": "重复",
          "summary_zh": "重复 id 夹具。",
          "learning_outcomes": ["x"],
          "labs": [{"lab_id": "lab91_probe_lab", "role": "core",
                    "estimated_effort_minutes": 60}]
        })" );
    validate( manifest );
    CHECK( hasIssue( issues, "duplicate_module_id", "m01_fixture" ) );
  }

  SECTION( "duplicate module index" )
  {
    std::string manifest = world.manifestJson();
    const auto pos = manifest.rfind( "]" );
    REQUIRE( pos != std::string::npos );
    manifest.insert( pos, R"(,
        {
          "id": "m02_dup_index",
          "index": 1,
          "title": "Dup",
          "title_zh": "重复序号",
          "summary_zh": "重复序号夹具。",
          "learning_outcomes": ["x"],
          "labs": [{"lab_id": "lab91_probe_lab", "role": "core",
                    "estimated_effort_minutes": 60}]
        })" );
    validate( manifest );
    CHECK( hasIssue( issues, "duplicate_module_index" ) );
  }

  SECTION( "prerequisite pointing at an unknown module" )
  {
    std::string manifest = world.manifestJson();
    manifest.replace( manifest.find( "\"prerequisite_modules\": []" ),
                      std::string( "\"prerequisite_modules\": []" ).size(),
                      "\"prerequisite_modules\": [\"m99_ghost\"]" );
    validate( manifest );
    CHECK( hasIssue( issues, "unknown_module_reference", "m99_ghost" ) );
  }

  SECTION( "prerequisite cycle" )
  {
    std::string manifest = world.manifestJson();
    const auto pos = manifest.rfind( "]" );
    REQUIRE( pos != std::string::npos );
    manifest.insert( pos, R"(,
        {
          "id": "m02_cyclic",
          "index": 2,
          "title": "Cyclic",
          "title_zh": "环",
          "summary_zh": "环夹具。",
          "learning_outcomes": ["x"],
          "prerequisite_modules": ["m01_fixture"],
          "labs": [{"lab_id": "lab91_probe_lab", "role": "core",
                    "estimated_effort_minutes": 60}]
        })" );
    // m01_fixture now requires m02_cyclic, closing the loop.
    manifest.replace( manifest.find( "\"prerequisite_modules\": []" ),
                      std::string( "\"prerequisite_modules\": []" ).size(),
                      "\"prerequisite_modules\": [\"m02_cyclic\"]" );
    validate( manifest );
    CHECK( hasIssue( issues, "cyclic_prerequisites" ) );
  }

  SECTION( "unresolvable lab reference" )
  {
    validate( world.manifestJson(
      "{\"lab_id\": \"lab99_ghost\", \"role\": \"core\", \"estimated_effort_minutes\": 60}" ) );
    CHECK( hasIssue( issues, "unknown_lab_reference", "lab99_ghost" ) );
  }

  SECTION( "external role requires an out_of_scope declaration in the registry" )
  {
    validate( world.manifestJson(
      "{\"lab_id\": \"temporal_track_lab\", \"role\": \"external\", \"estimated_effort_minutes\": 60}" ) );
    CHECK( hasIssue( issues, "undeclared_external_lab", "temporal_track_lab" ) );
  }

  SECTION( "external lab declared out_of_scope resolves" )
  {
    writeFile( world.dir.path() / "data" / "labs" / "lab-registry.json",
               registryJson( "", "", "", "temporal_track_lab" ) );
    validate( world.manifestJson(
      "{\"lab_id\": \"temporal_track_lab\", \"role\": \"external\", \"estimated_effort_minutes\": 60}" ) );
    CHECK( issues.empty() );
    CHECK( resolveLabReference( "temporal_track_lab", world.paths ) == "external" );
  }

  SECTION( "unknown data pack" )
  {
    validate( world.manifestJson(
      "{\"lab_id\": \"" + world.labId + "\", \"role\": \"core\", "
      "\"estimated_effort_minutes\": 60, \"required_data_packs\": [\"ghost_pack\"]}" ) );
    CHECK( hasIssue( issues, "unknown_data_pack", "ghost_pack" ) );
  }

  SECTION( "pack file with a foreign schema is not a pack" )
  {
    writeFile( world.dir.path() / "data" / "labs" / "packs" / "impostor.pack.json",
               "{\"schema_version\": \"someone.elses/9\", \"inputs\": []}" );
    validate( world.manifestJson(
      "{\"lab_id\": \"" + world.labId + "\", \"role\": \"core\", "
      "\"estimated_effort_minutes\": 60, \"required_data_packs\": [\"impostor\"]}" ) );
    CHECK( hasIssue( issues, "unknown_data_pack", "impostor" ) );
  }

  SECTION( "empty learning outcomes" )
  {
    std::string manifest = world.manifestJson();
    manifest.replace( manifest.find( "\"learning_outcomes\": [\"能独立完成夹具实验\"]" ),
                      std::string( "\"learning_outcomes\": [\"能独立完成夹具实验\"]" ).size(),
                      "\"learning_outcomes\": []" );
    validate( manifest );
    CHECK( hasIssue( issues, "empty_learning_outcomes" ) );
  }

  SECTION( "nonpositive effort at module and lab level" )
  {
    std::string manifest = world.manifestJson();
    manifest.replace( manifest.find( "\"estimated_effort_minutes\": 120,\n          \"labs\"" ),
                      std::string( "\"estimated_effort_minutes\": 120,\n          \"labs\"" ).size(),
                      "\"estimated_effort_minutes\": 0,\n          \"labs\"" );
    validate( manifest );
    CHECK( hasIssue( issues, "nonpositive_effort", "m01_fixture" ) );

    validate( world.manifestJson(
      "{\"lab_id\": \"" + world.labId + "\", \"role\": \"core\", \"estimated_effort_minutes\": -5}" ) );
    CHECK( hasIssue( issues, "nonpositive_effort", world.labId ) );
  }

  SECTION( "invalid lab role" )
  {
    validate( world.manifestJson(
      "{\"lab_id\": \"" + world.labId + "\", \"role\": \"bonus\", \"estimated_effort_minutes\": 60}" ) );
    CHECK( hasIssue( issues, "invalid_lab_role" ) );
  }

  SECTION( "wrong-typed containers are rejected, never silently skipped" )
  {
    FixtureWorld world;
    std::string manifest = world.manifestJson();
    const auto pos = manifest.rfind( "}" );
    REQUIRE( pos != std::string::npos );
    manifest.insert( pos, R"(,
        "forward_references": 42)" );
    validate( manifest );
    CHECK( hasIssue( issues, "missing_field", "forward_references" ) );
  }

  SECTION( "non-object teacher_notes is rejected" )
  {
    FixtureWorld world;
    validate( world.manifestJson(
      "{\"lab_id\": \"" + world.labId + "\", \"role\": \"core\", "
      "\"estimated_effort_minutes\": 60, \"teacher_notes\": \"读文档\"}" ) );
    CHECK( hasIssue( issues, "missing_field", "teacher_notes" ) );
  }

  SECTION( "unknown key inside a lab reference is rejected" )
  {
    validate( world.manifestJson(
      "{\"lab_id\": \"" + world.labId + "\", \"role\": \"core\", "
      "\"estimated_effort_minutes\": 60, \"auto_grade\": true}" ) );
    CHECK( hasIssue( issues, "unknown_key", "auto_grade" ) );
  }
}

TEST_CASE( "curriculum: lab reference resolution chain", "[curriculum][resolve]" )
{
  SECTION( "direct labspec" )
  {
    FixtureWorld world;
    CHECK( resolveLabReference( world.labId, world.paths ) == "labspec" );
  }

  SECTION( "registry canonical id" )
  {
    FixtureWorld world;
    writeFile( world.dir.path() / "data" / "labs" / "lab95_renamed.lab.json",
               labSpecJson( "old_renamed_lab", 1 ) );
    writeFile( world.dir.path() / "data" / "labs" / "lab-registry.json",
               registryJson( "lab95_renamed", "data/labs/lab95_renamed.lab.json" ) );
    CHECK( resolveLabReference( "lab95_renamed", world.paths ) == "registry" );
  }

  SECTION( "registry alias" )
  {
    FixtureWorld world;
    writeFile( world.dir.path() / "data" / "labs" / "lab95_renamed.lab.json",
               labSpecJson( "old_renamed_lab", 1 ) );
    writeFile( world.dir.path() / "data" / "labs" / "lab-registry.json",
               registryJson( "lab95_renamed", "data/labs/lab95_renamed.lab.json",
                             "old_renamed_lab" ) );
    CHECK( resolveLabReference( "old_renamed_lab", world.paths ) == "registry" );
  }

  SECTION( "labspec file with mismatched id does not resolve" )
  {
    FixtureWorld world;
    writeFile( world.dir.path() / "data" / "labs" / "lab96_impostor.lab.json",
               labSpecJson( "lab97_someone_else", 2 ) );
    CHECK( resolveLabReference( "lab96_impostor", world.paths ) == "unknown" );
  }

  SECTION( "v3 lab document resolves as a labspec" )
  {
    // spec_version 3 is the shipped, loader-supported generation (ADR 0174):
    // the router must not strand a legal v3 document as `unknown`. The
    // router only routes — it does not inspect the runtime block itself.
    FixtureWorld world;
    writeFile( world.dir.path() / "data" / "labs" / "lab93_runtime.lab.json",
               labSpecJson( "lab93_runtime", 3 ) );
    CHECK( resolveLabReference( "lab93_runtime", world.paths ) == "labspec" );
  }

  SECTION( "unsupported spec_version does not resolve" )
  {
    FixtureWorld world;
    writeFile( world.dir.path() / "data" / "labs" / "lab97_future.lab.json",
               labSpecJson( "lab97_future", 4 ) );
    CHECK( resolveLabReference( "lab97_future", world.paths ) == "unknown" );
  }

  SECTION( "unknown stays unknown" )
  {
    FixtureWorld world;
    CHECK( resolveLabReference( "lab99_ghost", world.paths ) == "unknown" );
  }
}

TEST_CASE( "curriculum: shipped manifest loads against the real repo data",
           "[curriculum][shipped]" )
{
  const std::filesystem::path source = SICNU_TEST_SOURCE_DIR;
  CurriculumCatalog &catalog = CurriculumCatalog::instance();
  catalog.setPaths( CurriculumPaths{} );
  catalog.setDirectory( ( source / "data" / "curriculum" ).string() );
  catalog.setPaths( CurriculumPaths{ ( source / "data" / "labs" ).string(),
                                     ( source / "data" / "labs" / "packs" ).string() } );
  catalog.reload();

  // Content floor (track DoD): >=8 modules, 12..16 labs, all references
  // resolve, no issue of any code.
  if ( catalog.status() != "ok" )
  {
    for ( const auto &problem : catalog.loadProblems() )
      FAIL( "shipped manifest problem: " << problem );
  }
  CHECK( catalog.moduleIds().size() >= 8 );
  const auto labs = catalog.labIds();
  CHECK( labs.size() >= 12 );
  // 16 owned labs + 1 read-only external reference (temporal track).
  CHECK( labs.size() <= 17 );

  int externalCount = 0;
  for ( const auto &labId : labs )
  {
    const std::string resolution = catalog.labResolution( labId );
    if ( resolution == "external" )
    {
      ++externalCount;
      continue;
    }
    INFO( "lab " << labId << " resolves as " << resolution );
    CHECK( ( resolution == "labspec" || resolution == "registry" ) );
  }
  // The temporal module references the temporal track's lab read-only.
  CHECK( externalCount == 1 );

  // Every declared pack exists (offline resolvability).
  const Json::Value modules = catalog.manifest()["modules"];
  for ( const auto &module : modules )
    for ( const auto &lab : module["labs"] )
      for ( const auto &pack : lab["required_data_packs"] )
        CHECK( std::filesystem::exists( source / "data" / "labs" / "packs" /
                                        ( pack.asString() + ".pack.json" ) ) );
}

// ===========================================================================
// Slice C — availability report (probe-injected)
// ===========================================================================

TEST_CASE( "curriculum: availability report three operator states", "[curriculum][availability]" )
{
  FixtureWorld world( "rs:fixture_op" );
  auto catalog = world.load( world.manifestJson() );
  REQUIRE( catalog.status() == "ok" );

  SECTION( "available: registered + capability note" )
  {
    const Json::Value report = buildAvailabilityReport(
      catalog.manifest(), world.paths.labsDir, world.paths.packsDir, fakeProbes( true, true ) );
    REQUIRE( report["ok"].asBool() );
    const Json::Value lab = report["modules"][0]["labs"][0];
    CHECK( lab["resolvable"] == "labspec" );
    REQUIRE( lab["operators"].size() == 1 );
    CHECK( lab["operators"][0]["operator_id"] == "rs:fixture_op" );
    CHECK( lab["operators"][0]["state"] == "available" );
    REQUIRE( lab["data_packs"].size() == 1 );
    CHECK( lab["data_packs"][0]["name"] == world.packName );
    CHECK( lab["data_packs"][0]["present"] == true );
  }

  SECTION( "registered without capability note is warning-grade, not failure" )
  {
    const Json::Value report = buildAvailabilityReport(
      catalog.manifest(), world.paths.labsDir, world.paths.packsDir, fakeProbes( true, false ) );
    REQUIRE( report["ok"].asBool() );
    CHECK( report["modules"][0]["labs"][0]["operators"][0]["state"] ==
           "registered_no_capability_note" );
  }

  SECTION( "unknown operator is error-grade" )
  {
    const Json::Value report = buildAvailabilityReport(
      catalog.manifest(), world.paths.labsDir, world.paths.packsDir, fakeProbes( false, false ) );
    CHECK( report["ok"].asBool() );  // the report itself succeeds; the STATE says unknown
    CHECK( report["modules"][0]["labs"][0]["operators"][0]["state"] == "unknown" );
  }

  SECTION( "missing pack is reported as absent" )
  {
    std::filesystem::remove( world.dir.path() / "data" / "labs" / "packs" /
                             ( world.packName + ".pack.json" ) );
    const Json::Value report = buildAvailabilityReport(
      catalog.manifest(), world.paths.labsDir, world.paths.packsDir, fakeProbes( true, true ) );
    CHECK( report["modules"][0]["labs"][0]["data_packs"][0]["present"] == false );
  }
}

TEST_CASE( "curriculum: forward references are surfaced verbatim and honestly",
           "[curriculum][availability]" )
{
  FixtureWorld world;
  std::string manifest = world.manifestJson();
  const auto pos = manifest.rfind( "}" );
  REQUIRE( pos != std::string::npos );
  manifest.insert( pos,
    R"(,
      "forward_references": [
        {"capability": "rs:infer",
         "reason_zh": "模型权重不入库（离线约束）",
         "wiring": "docs/integration.md#model-inference"}
      ])" );
  auto catalog = world.load( manifest );
  REQUIRE( catalog.status() == "ok" );
  const Json::Value report = buildAvailabilityReport(
    catalog.manifest(), world.paths.labsDir, world.paths.packsDir, fakeProbes( false, false ) );
  REQUIRE( report["forward_references"].size() == 1 );
  CHECK( report["forward_references"][0]["capability"] == "rs:infer" );
  CHECK( report["forward_references"][0]["state"] == "declared_forward_reference" );
  CHECK( report["forward_references"][0]["reason_zh"] == "模型权重不入库（离线约束）" );
}

// ===========================================================================
// Slice D — progress value model
// ===========================================================================

namespace
{

Json::Value manifestWithTwoModules()
{
  FixtureWorld world;
  const std::string manifestText = R"({
      "schema": "sicnu.curriculum/1",
      "id": "fixture_course",
      "title": "Fixture Course",
      "title_zh": "夹具课程",
      "modules": [
        {
          "id": "m01_fixture", "index": 1,
          "title": "One", "title_zh": "一", "summary_zh": "一。",
          "learning_outcomes": ["x"],
          "labs": [
            {"lab_id": "lab90_probe_lab", "role": "core", "estimated_effort_minutes": 60},
            {"lab_id": "lab91_probe_lab", "role": "core", "estimated_effort_minutes": 60}
          ]
        },
        {
          "id": "m02_fixture", "index": 2,
          "title": "Two", "title_zh": "二", "summary_zh": "二。",
          "learning_outcomes": ["y"],
          "labs": [
            {"lab_id": "lab91_probe_lab", "role": "optional", "estimated_effort_minutes": 60}
          ]
        }
      ]
    })";
  return parseJson( manifestText );
}

} // namespace

TEST_CASE( "curriculum progress: mark/idempotency/conflict/unknown", "[curriculum][progress]" )
{
  Json::Value doc = CurriculumProgress::emptyDoc();
  const std::vector<std::string> known = { "lab90_probe_lab", "lab91_probe_lab" };

  std::vector<CurriculumProgressIssue> issues;
  REQUIRE( CurriculumProgress::validateDoc( doc, known ).empty() );

  SECTION( "mark completes and is idempotent for identical evidence" )
  {
    REQUIRE( CurriculumProgress::markCompleted( doc, "lab90_probe_lab", "out/report.json",
                                                "2026-03-12T08:00:00Z", known, &issues ) );
    REQUIRE( CurriculumProgress::markCompleted( doc, "lab90_probe_lab", "out/report.json",
                                                "2026-03-12T08:00:00Z", known, &issues ) );
    CHECK( issues.empty() );
    CHECK( doc["completed"].size() == 1 );
    CHECK( CurriculumProgress::validateDoc( doc, known ).empty() );
  }

  SECTION( "conflicting evidence is refused, never overwritten" )
  {
    REQUIRE( CurriculumProgress::markCompleted( doc, "lab90_probe_lab", "out/a.json",
                                                "2026-03-12T08:00:00Z", known, &issues ) );
    issues.clear();
    CHECK_FALSE( CurriculumProgress::markCompleted( doc, "lab90_probe_lab", "out/b.json",
                                                    "2026-03-12T08:00:00Z", known, &issues ) );
    CHECK( hasProgressIssue( issues, "progress_conflicting_evidence" ) );
    CHECK( doc["completed"]["lab90_probe_lab"]["evidence"] == "out/a.json" );
  }

  SECTION( "unknown lab is refused" )
  {
    CHECK_FALSE( CurriculumProgress::markCompleted( doc, "lab99_ghost", "x", "t", known, &issues ) );
    CHECK( hasProgressIssue( issues, "progress_unknown_lab" ) );
  }

  SECTION( "missing evidence or timestamp is refused (determinism contract)" )
  {
    issues.clear();
    CHECK_FALSE( CurriculumProgress::markCompleted( doc, "lab90_probe_lab", "", "t", known, &issues ) );
    CHECK( hasProgressIssue( issues, "progress_missing_field" ) );
    issues.clear();
    CHECK_FALSE( CurriculumProgress::markCompleted( doc, "lab90_probe_lab", "e", "", known, &issues ) );
    CHECK( hasProgressIssue( issues, "progress_missing_field" ) );
  }

  SECTION( "invalid doc shape is refused" )
  {
    Json::Value bogus;
    bogus["schema"] = "someone.else/1";
    CHECK_FALSE( CurriculumProgress::markCompleted( bogus, "lab90_probe_lab", "e", "t", known, &issues ) );
    CHECK( hasProgressIssue( issues, "progress_invalid_doc" ) );
  }
}

TEST_CASE( "curriculum progress: completion projection is deterministic",
           "[curriculum][progress]" )
{
  const Json::Value manifest = manifestWithTwoModules();
  Json::Value doc = CurriculumProgress::emptyDoc();
  std::vector<CurriculumProgressIssue> issues;
  REQUIRE( CurriculumProgress::markCompleted( doc, "lab91_probe_lab", "evidence",
                                              "2026-03-12T08:00:00Z", { "lab91_probe_lab" }, &issues ) );

  const auto stats = CurriculumProgress::moduleCompletion( doc, manifest );
  REQUIRE( stats.size() == 2 );
  CHECK( stats[0].moduleId == "m01_fixture" );
  CHECK( stats[0].total == 2 );
  CHECK( stats[0].done == 1 );
  CHECK( stats[1].moduleId == "m02_fixture" );
  CHECK( stats[1].total == 1 );
  CHECK( stats[1].done == 1 );

  const Json::Value summaryDoc = CurriculumProgress::summary( doc, manifest );
  CHECK( summaryDoc["schema"] == "sicnu.curriculum.progress.summary/1" );
  CHECK( summaryDoc["ok"] == true );
  // The shared lab91 counts once overall (dedupe by lab id), so 1 of 2 labs.
  CHECK( summaryDoc["overall_percent"] == 50 );
  CHECK( summaryDoc["modules"][0]["percent"] == 50 );
  CHECK( summaryDoc["modules"][1]["percent"] == 100 );
  CHECK( summaryDoc["modules"][0]["missing_lab_ids"][0] == "lab90_probe_lab" );

  // Byte-stable serialization (deterministic replay / persistence contract).
  const std::string first = CurriculumProgress::serialize( summaryDoc );
  const std::string second = CurriculumProgress::serialize( summaryDoc );
  CHECK( first == second );
  CHECK( !first.empty() );

  // Empty progress → 0% everywhere, still valid.
  const Json::Value emptySummary =
    CurriculumProgress::summary( CurriculumProgress::emptyDoc(), manifest );
  CHECK( emptySummary["overall_percent"] == 0 );
}

TEST_CASE( "curriculum progress: catalog projection surfaces typed issues",
           "[curriculum][progress]" )
{
  FixtureWorld world;
  auto catalog = world.load( world.manifestJson() );
  REQUIRE( catalog.status() == "ok" );

  SECTION( "valid projection" )
  {
    Json::Value doc = CurriculumProgress::emptyDoc();
    std::vector<CurriculumProgressIssue> issues;
    REQUIRE( CurriculumProgress::markCompleted( doc, world.labId, "e", "t0",
                                                catalog.labIds(), &issues ) );
    const Json::Value projection = catalog.progressFor( doc );
    CHECK( projection["ok"] == true );
    CHECK( projection["overall_percent"] == 100 );
  }

  SECTION( "unknown lab in the progress doc degrades to a typed failed summary" )
  {
    Json::Value doc = CurriculumProgress::emptyDoc();
    doc["completed"]["lab99_ghost"] = "not even an object";
    const Json::Value projection = catalog.progressFor( doc );
    CHECK( projection["ok"] == false );
    REQUIRE_FALSE( projection["issues"].isNull() );
    bool sawUnknown = false;
    for ( const auto &issue : projection["issues"] )
      if ( issue["code"] == "progress_unknown_lab" ) sawUnknown = true;
    CHECK( sawUnknown );
  }
}
