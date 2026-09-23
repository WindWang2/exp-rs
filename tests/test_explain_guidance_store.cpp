// tests/test_explain_guidance_store.cpp
//
// RS14-15 Slice C coverage: the file-backed authored-guidance store
// (exp.step_guidance.v1). Loading is fail-closed — invalid files are typed
// problems and skipped, never partially applied — and hostile documents
// (deep JSON bombs, wrong-typed fields, unknown keys) are typed refusals.
// Role-specific entries win over the generic entry; duplicates keep first.
#include <catch2/catch_test_macros.hpp>

#include "explain/guidance_store.h"

#include <json/json.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define SICNU_TEST_GETPID ::_getpid
#else
#include <unistd.h>
#define SICNU_TEST_GETPID ::getpid
#endif

using namespace sicnu::explain;

namespace
{

struct TempDir
{
  std::filesystem::path path;
  TempDir()
  {
    path = std::filesystem::temp_directory_path()
           / ( "explain_guidance_store_test_" + std::to_string( SICNU_TEST_GETPID() ) );
    std::filesystem::create_directories( path );
  }
  ~TempDir() { std::filesystem::remove_all( path ); }
  std::string write( const std::string &name, const std::string &content ) const
  {
    const std::filesystem::path file = path / name;
    std::ofstream out( file, std::ios::binary );
    out << content;
    return file.filename().string();
  }
};

std::string validEntry( const std::string &operatorId, const std::string &role = "" )
{
  std::string json = "{\n  \"schema\": \"exp.step_guidance.v1\",\n";
  json += "  \"operatorId\": \"" + operatorId + "\",\n";
  if ( !role.empty() )
    json += "  \"role\": \"" + role + "\",\n";
  json += "  \"purpose\": \"teaching purpose\",\n";
  json += "  \"whenToUse\": \"when teaching\",\n";
  json += "  \"prerequisitesNote\": [\"know what NDVI is\"],\n";
  json += "  \"assumptions\": [\"Lambertian surface\"],\n";
  json += "  \"parameterRationale\": [ { \"parameter\": \"threshold\", \"rationale\": \"balance\", "
           "\"misconfigurationConsequence\": \"omission\" } ],\n";
  json += "  \"stateNarrative\": { \"before\": \"DN\", \"after\": \"TOA\" },\n";
  json += "  \"skipConsequence\": { \"summary\": \"index missing\", \"downstreamRoles\": [\"classification\"] },\n";
  json += "  \"references\": [ { \"title\": \"A textbook\", \"kind\": \"textbook\", \"locator\": \"ch. 4\" } ],\n";
  json += "  \"teachingNote\": \"mention band roles\"\n";
  json += "}";
  return json;
}

bool hasProblem( const std::vector<GuidanceLoadProblem> &problems, const std::string &code )
{
  for ( const GuidanceLoadProblem &problem : problems )
    if ( problem.code == code )
      return true;
  return false;
}

} // namespace

TEST_CASE( "valid guidance files load and answer role-aware lookups",
           "[explain][guidance_store]" )
{
  TempDir dir;
  dir.write( "ndvi.json", validEntry( "rs:ndvi" ) );
  dir.write( "ndvi_teacher.json", validEntry( "rs:ndvi", "teacher" ) );

  std::vector<GuidanceLoadProblem> problems;
  const std::unique_ptr<GuidanceStore> store = GuidanceStore::loadFromDirectory( dir.path.string(), problems );
  REQUIRE( store != nullptr );
  CHECK( store->entryCount() == 2 );
  CHECK( problems.empty() );

  // Role-specific wins over generic; unknown roles fall back to generic.
  const std::optional<StepGuidance> generic = store->guidanceFor( "rs:ndvi", "" );
  REQUIRE( generic.has_value() );
  CHECK( generic->role.empty() );
  const std::optional<StepGuidance> teacher = store->guidanceFor( "rs:ndvi", "teacher" );
  REQUIRE( teacher.has_value() );
  CHECK( teacher->role == "teacher" );
  const std::optional<StepGuidance> fallback = store->guidanceFor( "rs:ndvi", "student" );
  REQUIRE( fallback.has_value() );
  CHECK( fallback->role.empty() );
  CHECK( !store->guidanceFor( "rs:ghost", "" ).has_value() );

  CHECK( generic->parameterRationale.size() == 1 );
  CHECK( generic->parameterRationale.front().parameter == "threshold" );
  CHECK( generic->stateNarrative.has_value() );
  CHECK( generic->stateNarrative->before == "DN" );
}

TEST_CASE( "invalid files are typed problems and skipped, never partially applied",
           "[explain][guidance_store][hostile]" )
{
  TempDir dir;
  // unsupported schema version
  {
    std::string text = validEntry( "rs:b" );
    text.replace( text.find( "exp.step_guidance.v1" ), 20, "exp.step_guidance.v9" );
    dir.write( "bad_version.json", text );
  }
  dir.write( "unknown_key.json", R"({
    "schema": "exp.step_guidance.v1",
    "operatorId": "rs:c",
    "purpose": "p",
    "narrativeExtra": 1
  })" );
  dir.write( "bad_json.json", "{ \"schema\": exp.step_guidance.v1 " );
  dir.write( "bad_token.json", validEntry( "rs:d" ) );
  {
    std::string text = validEntry( "rs:d" );
    text.replace( text.find( "\"TOA\"" ), 5, "\"PLANKTON\"" );
    dir.write( "bad_token.json", text );
  }
  dir.write( "good.json", validEntry( "rs:e" ) );

  std::vector<GuidanceLoadProblem> problems;
  const std::unique_ptr<GuidanceStore> store = GuidanceStore::loadFromDirectory( dir.path.string(), problems );
  REQUIRE( store != nullptr );
  CHECK( store->entryCount() == 1 );
  CHECK( store->guidanceFor( "rs:e", "" ).has_value() );
  CHECK( hasProblem( problems, "schema_version_unsupported" ) );
  CHECK( hasProblem( problems, "unknown_key" ) );
  CHECK( hasProblem( problems, "parse_failed" ) );
  CHECK( hasProblem( problems, "invalid_value" ) );
}

TEST_CASE( "duplicates keep the first-loaded file", "[explain][guidance_store]" )
{
  TempDir dir;
  dir.write( "a_first.json", validEntry( "rs:dup" ) );
  dir.write( "b_second.json", validEntry( "rs:dup" ) );

  std::vector<GuidanceLoadProblem> problems;
  const std::unique_ptr<GuidanceStore> store = GuidanceStore::loadFromDirectory( dir.path.string(), problems );
  CHECK( store->entryCount() == 1 );
  CHECK( hasProblem( problems, "duplicate_entry" ) );
}

TEST_CASE( "deep JSON bombs are typed refusals instead of stack exhaustion",
           "[explain][guidance_store][hostile]" )
{
  TempDir dir;
  std::string bomb = "{\"schema\": \"exp.step_guidance.v1\", \"operatorId\": \"rs:deep\", "
                     "\"purpose\": \"p\", \"deep\": ";
  for ( int i = 0; i < 500; ++i )
    bomb += "[";
  bomb += "1";
  for ( int i = 0; i < 500; ++i )
    bomb += "]";
  bomb += "}";

  dir.write( "bomb.json", bomb );
  std::vector<GuidanceLoadProblem> problems;
  const std::unique_ptr<GuidanceStore> store = GuidanceStore::loadFromDirectory( dir.path.string(), problems );
  REQUIRE( store != nullptr );
  CHECK( store->entryCount() == 0 );
  CHECK( hasProblem( problems, "parse_failed" ) );
}

TEST_CASE( "wrong-typed and whitespace-laden identities are refused",
           "[explain][guidance_store][hostile]" )
{
  TempDir dir;
  dir.write( "numeric_operator.json", R"({
    "schema": "exp.step_guidance.v1",
    "operatorId": 42,
    "purpose": "p"
  })" );
  dir.write( "spacey_operator.json", R"({
    "schema": "exp.step_guidance.v1",
    "operatorId": "rs: spaced",
    "purpose": "p"
  })" );
  dir.write( "empty_purpose.json", R"({
    "schema": "exp.step_guidance.v1",
    "operatorId": "rs:fine",
    "purpose": ""
  })" );

  std::vector<GuidanceLoadProblem> problems;
  const std::unique_ptr<GuidanceStore> store = GuidanceStore::loadFromDirectory( dir.path.string(), problems );
  CHECK( store->entryCount() == 0 );
  CHECK( problems.size() == 3 );
}

TEST_CASE( "a missing directory is a recorded problem, not an empty success",
           "[explain][guidance_store]" )
{
  std::vector<GuidanceLoadProblem> problems;
  const std::unique_ptr<GuidanceStore> store = GuidanceStore::loadFromDirectory(
    "/definitely/not/a/real/dir/explain", problems );
  REQUIRE( store != nullptr );
  CHECK( store->entryCount() == 0 );
  CHECK( hasProblem( problems, "directory_missing" ) );
}
