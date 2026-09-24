// test_teaching_foundation_e2e.cpp — teaching foundation E2E (no UI).
//
// Drives the full offline chain the teaching cockpit consumes, entirely on
// the existing authorities and without touching src/teaching/**:
//
//   seeded sample foundry (GDAL emit + manifest digests)
//     → shipped lab documents (registry-resolved wrappers, recipe compile)
//     → recipe validation + committed-artifact drift gate
//     → pack / grading / pipeline reference resolution over the repo tree
//     → LabSpec v3 runtime session whose gate checkpoint verifies a foundry
//       artifact through a real filesystem probe.
//
// Qt-free like its ingredients (jsoncpp + std + GDAL via the foundry). This
// test is the integration seam the hardening campaign asserts: every layer
// boundary below is a real shipped contract, not a mock.

#include <catch2/catch_test_macros.hpp>

#include "lab/checkpoint_verify.h"
#include "lab/session_state.h"
#include "lab/session_store.h"
#include "lab/sha256.h"
#include "lab/spec_runtime.h"
#include "platform/portable.h"
#include "recipes/lab_document.h"
#include "recipes/lab_source.h"
#include "recipes/provider_interfaces.h"
#include "recipes/recipe_compiler.h"
#include "recipes/recipe_validator.h"
#include "recipes/scientific_recipe.h"
#include "sample_foundry.h"

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::lab;
using namespace sicnu::recipes;
namespace fs = std::filesystem;

namespace
{

std::string sourceDir()
{
#ifdef CMAKE_SOURCE_DIR
  return CMAKE_SOURCE_DIR;
#else
  return fs::current_path().string();
#endif
}

std::string makeTempDir( const char *tag )
{
  static unsigned counter = 0;
  const fs::path dir = fs::temp_directory_path() /
                       ( std::string( "sicnu_teaching_e2e_" ) + tag + "_" +
                         std::to_string( ++counter ) + "_" + std::to_string( sicnu::portable::pid() ) );
  fs::create_directories( dir );
  return dir.string();
}

Json::Value parseJsonText( const std::string &text )
{
  Json::Value doc;
  Json::CharReaderBuilder builder;
  builder[ "stackLimit" ] = 64;
  std::string errors;
  std::istringstream stream( text );
  REQUIRE( Json::parseFromStream( builder, stream, &doc, &errors ) );
  return doc;
}

std::string readFileBytes( const fs::path &path )
{
  std::ifstream in( path, std::ios::binary );
  REQUIRE( in.good() );
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

/// FileProbe rooted at a directory — the sandboxed stand-in for the real
/// project tree a teaching host would inject.
struct RootedProbe final : public FileProbe
{
  std::string root;

  explicit RootedProbe( std::string base )
    : root( std::move( base ) )
  {
  }

  fs::path resolve( const std::string &relPath ) const { return fs::path( root ) / relPath; }
  bool exists( const std::string &relPath ) override
  {
    std::error_code ec;
    return fs::is_regular_file( resolve( relPath ), ec );
  }
  long long fileSize( const std::string &relPath ) override
  {
    std::error_code ec;
    const auto size = fs::file_size( resolve( relPath ), ec );
    return ec ? -1 : static_cast<long long>( size );
  }
  bool readFile( const std::string &relPath, std::string &out, long long budgetBytes ) override
  {
    std::error_code ec;
    const auto size = fs::file_size( resolve( relPath ), ec );
    if ( ec || static_cast<long long>( size ) > budgetBytes )
      return false;
    out = readFileBytes( resolve( relPath ) );
    return true;
  }
};

/// LabSourceEntry ids for the shipped tree, loaded exactly like the CLI does.
/// Returns a reference to the function-local static: callers take
/// `LabDocument` copies out of this vector, so returning by value would hand
/// out references into a dead temporary.
const std::vector<LabSourceEntry> &shippedLabs()
{
  static const std::vector<LabSourceEntry> entries = []
  {
    const std::string root = sourceDir();
    std::vector<LabDocumentError> errors;
    auto e = loadLabDirectory( root + "/data/labs",
                               root + "/data/labs/lab-registry.json", errors );
    for ( const auto &err : errors )
      WARN( "lab-source error: " << err.toString() );
    return e;
  }();
  return entries;
}

const LabDocument &shippedLab( const std::string &canonicalId )
{
  for ( const LabSourceEntry &e : shippedLabs() )
    if ( e.canonicalId == canonicalId )
      return e.document;
  FAIL( "shipped lab not found: " << canonicalId );
  static LabDocument empty;
  return empty;
}

} // namespace

TEST_CASE( "E2E: foundry emits a seeded dataset that verifies against its own manifest",
           "[teaching_e2e][foundry]" )
{
  sicnu::foundry::Options options;
  options.profile = sicnu::foundry::Profile::Lab;
  options.seed = 42;
  options.products = { sicnu::foundry::Product::LandsatSample };
  options.out_dir = makeTempDir( "foundry" );

  sicnu::foundry::GenerateResult first;
  REQUIRE( sicnu::foundry::generate( options, &first ).ok );
  REQUIRE( !first.files.empty() );
  REQUIRE( fs::is_regular_file( fs::path( options.out_dir ) / "manifest.json" ) );

  // The manifest's own digests re-verify from disk.
  sicnu::foundry::VerifyReport report;
  REQUIRE( sicnu::foundry::verifyDirectory( options.out_dir, &report ).ok );
  REQUIRE( report.files_checked == first.files.size() );

  // Same seed + profile → identical manifest bytes (per-host determinism).
  const std::string firstDir = options.out_dir;
  sicnu::foundry::GenerateResult second;
  options.out_dir = makeTempDir( "foundry2" );
  REQUIRE( sicnu::foundry::generate( options, &second ).ok );
  REQUIRE( second.manifest_bytes == first.manifest_bytes );

  std::error_code ec;
  fs::remove_all( firstDir, ec );
  fs::remove_all( options.out_dir, ec );
}

TEST_CASE( "E2E: shipped labs compile, validate and match the committed artifacts",
           "[teaching_e2e][recipes]" )
{
  const auto labs = shippedLabs();
  REQUIRE( labs.size() >= 17 ); // 16 canonical documents + the standalone D3 lab

  const SidecarOperatorCatalog ops( std::vector<std::string>{
    sourceDir() + "/data/processing/algorithm_meta",
    sourceDir() + "/data/agent/capabilities" } );
  REQUIRE( ops.operatorIds().size() > 100 );

  const LabDocument lab = shippedLab( "lab01_image_enhancement" );
  REQUIRE( !lab.steps.empty() );
  const CompileResult compiled = compileLabToRecipe( lab, ops );
  REQUIRE( !hasErrors( compiled.diagnostics ) );
  REQUIRE( recipeIsValid( compiled.recipe, nullptr ) );

  // Byte-level drift gate against the committed machine-readable recipe.
  const std::string committed = readFileBytes(
    fs::path( sourceDir() ) / "data/agent/scientific_recipes/lab.lab01_image_enhancement.json" );
  REQUIRE( committed == serializeRecipe( compiled.recipe ) );
}

TEST_CASE( "E2E: pack, grading and pipeline references resolve for every shipped lab",
           "[teaching_e2e][refs]" )
{
  const std::string root = sourceDir();
  const Json::Value registry =
    parseJsonText( readFileBytes( fs::path( root ) / "data/labs/lab-registry.json" ) );

  // Wrapper grading pointers must exist, parse and carry the rules schema —
  // the machine-side mirror of the registry gate.
  for ( const char *wrapperId : { "lab12_sar_processing", "lab13_hyperspectral_analysis",
                                  "lab14_cartographic_mapping", "lab16_accuracy_assessment" } )
  {
    const Json::Value wrapper = parseJsonText(
      readFileBytes( fs::path( root ) / "data/labs" / ( std::string( wrapperId ) + ".lab.json" ) ) );
    const std::string rules = wrapper[ "grading_rules" ].asString();
    INFO( wrapperId << " → " << rules );
    const Json::Value rulesDoc = parseJsonText( readFileBytes( fs::path( root ) / rules ) );
    REQUIRE( rulesDoc[ "schema_version" ].asString() == "sicnu.lab.rules/1" );
  }

  // Every registry canonical lab keeps a pack file (via alias_packs when the
  // pack is named after the legacy spelling).
  const Json::Value aliasPacks = registry[ "alias_packs" ];
  for ( const std::string &canonicalId : registry[ "canonical" ].getMemberNames() )
  {
    std::string packBase = canonicalId;
    for ( const std::string &alias : aliasPacks.getMemberNames() )
      if ( aliasPacks[ alias ].asString() == canonicalId )
        packBase = alias;
    const fs::path packPath =
      fs::path( root ) / "data/labs/packs" / ( packBase + ".pack.json" );
    INFO( canonicalId << " pack: " << packPath.string() );
    REQUIRE( fs::is_regular_file( packPath ) );
    const Json::Value pack = parseJsonText( readFileBytes( packPath ) );
    REQUIRE( pack[ "schema_version" ].asString() == "sicnu.lab-pack/1" );
    REQUIRE( pack[ "lab_id" ].asString() == packBase );
  }

  // The grading corpus pack pins committed fixtures by sha256; the pins must
  // match the tree the classroom actually deploys.
  const Json::Value corpus = parseJsonText(
    readFileBytes( fs::path( root ) / "data/labs/packs/grading_corpus.pack.json" ) );
  REQUIRE( corpus[ "inputs" ].isArray() );
  std::size_t pinned = 0;
  for ( const Json::Value &input : corpus[ "inputs" ] )
  {
    if ( input[ "provenance" ].asString() != "committed-fixture" )
      continue;
    ++pinned;
    const fs::path fixture = fs::path( root ) / input[ "path" ].asString();
    REQUIRE( fs::is_regular_file( fixture ) );
    const std::string digest = sha256Hex( readFileBytes( fixture ) );
    INFO( "fixture: " << input[ "path" ].asString() );
    REQUIRE( digest == input[ "sha256" ].asString() );
  }
  REQUIRE( pinned >= 8 ); // the closed-form grading corpus is non-trivial
}

TEST_CASE( "E2E: a v3 runtime session gates on a foundry artifact and completes",
           "[teaching_e2e][runtime]" )
{
  // 1. Foundry emits the teaching data; its digest becomes the checkpoint pin.
  sicnu::foundry::Options options;
  options.profile = sicnu::foundry::Profile::Lab;
  options.seed = 42;
  options.products = { sicnu::foundry::Product::LandsatSample };
  options.out_dir = makeTempDir( "runtime" );
  sicnu::foundry::GenerateResult generated;
  REQUIRE( sicnu::foundry::generate( options, &generated ).ok );

  const std::string artifactBytes =
    readFileBytes( fs::path( options.out_dir ) / "landsat_sample.tif" );
  const std::string artifactDigest = sha256Hex( artifactBytes );

  // 2. A v3 lab document whose gate checkpoint consumes that artifact.
  const std::string docJson = R"JSON({
    "spec_version": 3,
    "id": "lab91_e2e_probe",
    "title": "E2E Probe",
    "objective": "verify the teaching chain",
    "steps": [ { "title": "inspect" }, { "title": "enhance", "operator_id": "rs:contrast_stretch" } ],
    "runtime": {
      "data_packs": [ "lab01_image_enhancement" ],
      "stages": [ {
        "id": "s1_enhance",
        "title": "Enhance",
        "title_zh": "增强",
        "objective": "stretch the sample",
        "step_indices": [ 0, 1 ],
        "allowed_tools": [ "rs:contrast_stretch" ],
        "checkpoints": [ {
          "id": "ckpt_stretch",
          "title": "Stretched",
          "title_zh": "已完成增强",
          "advance": "gate",
          "checks": [
            { "kind": "artifact_present", "path": "landsat_sample.tif", "min_bytes": 1,
              "sha256": ")JSON" + artifactDigest + R"JSON(" },
            { "kind": "operator_invoked", "operator_id": "rs:contrast_stretch" },
            { "kind": "question_answered", "question_id": "q_mean" }
          ]
        } ]
      } ],
      "questions": [ {
        "id": "q_mean", "prompt": "Roughly what reflectance range?", "kind": "numeric",
        "expected_numeric": { "min": 0.0, "max": 1.0 }
      } ],
      "hints": { "policy": { "escalation": [ "hint" ] } },
      "reproducibility": { "require_seed": true }
    }
  })JSON";

  const Json::Value doc = parseJsonText( docJson );
  const auto plan = parseRuntimeBlock( doc );
  REQUIRE( plan.ok );

  // 3. A student session: seed, act inside the whitelist, answer, verify.
  SessionMeta meta;
  meta.labId = "lab91_e2e_probe";
  meta.studentId = "student_e2e";
  meta.labSpecVersion = 3;
  meta.planSource = "authored_v3";
  meta.hasSeed = true;
  meta.seed = 42;

  const std::string fingerprint = specFingerprint( docJson );
  const std::string storeRoot = makeTempDir( "store" );
  LabSessionStore store( storeRoot );
  auto started = store.create( plan.value, meta, fingerprint );
  REQUIRE( started.ok );
  LabSession &session = started.value;

  sicnu::lab::ToolChoice choice;
  choice.stageId = "s1_enhance";
  choice.operatorId = "rs:contrast_stretch";
  REQUIRE( recordToolUse( session, plan.value, choice ).ok );
  REQUIRE( recordAnswer( session, plan.value, "q_mean", "", 0.42 ).ok );

  RootedProbe probe( options.out_dir );
  auto verdict = verifyCheckpoint( session, plan.value, "ckpt_stretch", probe );
  REQUIRE( verdict.ok );
  REQUIRE( verdict.value.verdict == Verdict::Pass );
  REQUIRE( session.stages[0].status == StageStatus::Advanced );

  REQUIRE( completeSession( session, plan.value ).ok );

  // 4. The completed ledger persists, resumes drift-free and stays canonical.
  REQUIRE( store.save( session ).ok );
  auto resumed = store.load( session.sessionId, fingerprint );
  REQUIRE( resumed.ok );
  REQUIRE( resumed.value.state == SessionState::Completed );
  REQUIRE( sessionToCanonicalBytes( resumed.value ) == sessionToCanonicalBytes( session ) );

  // Tampering with the spec bytes is refused on resume (spec_drift).
  auto drifted = store.load( session.sessionId, specFingerprint( docJson + " " ) );
  REQUIRE( !drifted.ok );
  REQUIRE( drifted.diagnostics.front().code == "lab.session.spec_drift" );

  std::error_code ec;
  fs::remove_all( options.out_dir, ec );
  fs::remove_all( storeRoot, ec );
}
