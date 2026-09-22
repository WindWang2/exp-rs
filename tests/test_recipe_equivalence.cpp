// test_recipe_equivalence.cpp — RS14-20 Slice G.
//
// Compiles the real shipped labs and proves the output is semantically
// equivalent to independently authored reference recipes
// (tests/data/scientific_recipes/reference/). The same comparator doubles as
// the drift gate between committed artifacts (data/agent/scientific_recipes/)
// and a fresh compile — references fix the *contract*, committed artifacts
// fix the *bytes*.
//
// Offline: labs + sidecar operator catalogs read straight from the source
// tree (CMAKE_SOURCE_DIR define).

#include <catch2/catch_test_macros.hpp>

#include "recipes/lab_document.h"
#include "recipes/lab_source.h"
#include "recipes/provider_interfaces.h"
#include "recipes/recipe_compiler.h"
#include "recipes/recipe_validator.h"
#include "recipes/scientific_recipe.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sicnu::recipes;
namespace fs = std::filesystem;

namespace {

std::string sourceDir()
{
#ifdef CMAKE_SOURCE_DIR
  return CMAKE_SOURCE_DIR;
#else
  return fs::current_path().string();
#endif
}

const std::vector<std::string> kExemplars = {
  "lab01_image_enhancement",
  "lab02_spectral_analysis",
  "lab05_terrain_analysis",
  "lab07_image_fusion",
  "lab11_obia_classification",
};

SidecarOperatorCatalog realCatalog()
{
  const std::string root = sourceDir();
  return SidecarOperatorCatalog( std::vector<std::string>{
    root + "/data/processing/algorithm_meta",
    root + "/data/agent/capabilities" } );
}

/// Labs loaded through the same pipeline the CLI uses (loadLabDirectory), so
/// provenance fields (repo-relative source_path, registry merges) match the
/// committed artifacts byte-for-byte.
const std::vector<LabSourceEntry> &shippedLabs()
{
  static const std::vector<LabSourceEntry> entries = []
  {
    const std::string root = sourceDir();
    std::vector<LabDocumentError> errors;
    auto e = loadLabDirectory( root + "/data/labs",
                               root + "/data/labs/lab-registry.json", errors );
    // Source-level failures are surfaced loudly, not swallowed.
    for ( const auto &err : errors )
      WARN( "lab-source error: " << err.toString() );
    return e;
  }();
  return entries;
}

LabDocument loadLabOrFail( const std::string &labId )
{
  for ( const auto &e : shippedLabs() )
    if ( e.canonicalId == labId )
      return e.document;
  FAIL( "lab not found in shipped scan: " << labId );
  return {};
}

Json::Value loadJsonOrFail( const fs::path &path )
{
  std::ifstream in( path, std::ios::binary );
  REQUIRE( in.good() );
  std::stringstream buffer;
  buffer << in.rdbuf();
  Json::Value doc;
  std::string error;
  REQUIRE( parseRecipeJson( buffer.str(), doc, &error ) );
  return doc;
}

} // namespace

TEST_CASE( "Sidecar catalog covers every exemplar operator", "[equivalence]" )
{
  const SidecarOperatorCatalog ops = realCatalog();
  REQUIRE( ops.operatorIds().size() > 100 ); // sanity: real sidecars loaded
  for ( const char *op : { "rs:contrast_stretch", "rs:spectral_index",
                           "rs:band_math", "rs:terrain_analysis",
                           "rs:image_fusion", "rs:obia_segment",
                           "rs:obia_classify", "opencv:gaussian_blur",
                           "opencv:sobel" } )
    REQUIRE( ops.hasOperator( op ) );
  // family:* records are catalog defaults, not executable operators.
  REQUIRE( !ops.hasOperator( "family:filter" ) );
}

TEST_CASE( "Compiled labs ≡ hand-authored reference recipes", "[equivalence]" )
{
  const SidecarOperatorCatalog ops = realCatalog();
  for ( const std::string &labId : kExemplars )
  {
    const LabDocument lab = loadLabOrFail( labId );
    const CompileResult result = compileLabToRecipe( lab, ops );

    INFO( "lab: " << labId );
    // No error-severity diagnostics on shipped exemplars.
    REQUIRE( !hasErrors( result.diagnostics ) );

    // Compiled output must pass the authoritative validator.
    RecipeDiagnostics valDiags;
    REQUIRE( recipeIsValid( result.recipe, &valDiags ) );

    const fs::path refPath = fs::path( sourceDir() ) / "tests/data" /
                             "scientific_recipes" / "reference" /
                             ( "lab." + labId + ".json" );
    const Json::Value reference = loadJsonOrFail( refPath );

    std::vector<std::string> diff;
    const bool same = recipesEquivalent( result.recipe, reference, &diff );
    for ( const auto &line : diff )
      INFO( "diff: " << line );
    REQUIRE( same );

    // Semantic parity must also hold in the teaching direction: the compiled
    // recipe carries every human-only boundary the author wrote.
    int humanStages = 0, operatorStages = 0;
    for ( const auto &s : result.recipe["stages"] )
    {
      if ( s.get( "kind", "" ).asString() == "human_only" )
        ++humanStages;
      if ( s.get( "kind", "" ).asString() == "operator" )
        ++operatorStages;
    }
    REQUIRE( humanStages >= 1 );   // every exemplar keeps a human boundary
    REQUIRE( operatorStages >= 1 ); // and at least one automatable stage
  }
}

TEST_CASE( "Committed artifacts are drift-free vs a fresh compile", "[equivalence]" )
{
  const SidecarOperatorCatalog ops = realCatalog();
  const fs::path artifactsDir =
    fs::path( sourceDir() ) / "data/agent/scientific_recipes";

  for ( const std::string &labId : kExemplars )
  {
    const LabDocument lab = loadLabOrFail( labId );
    const CompileResult result = compileLabToRecipe( lab, ops );

    const fs::path artifactPath = artifactsDir / ( "lab." + labId + ".json" );
    const Json::Value committed = loadJsonOrFail( artifactPath );

    INFO( "lab: " << labId );
    std::vector<std::string> diff;
    const bool same = recipesEquivalent( result.recipe, committed, &diff );
    for ( const auto &line : diff )
      INFO( "diff: " << line );
    REQUIRE( same );

    // Byte-level replay: the committed file must be exactly what the
    // deterministic serializer emits — no hand-edited drift.
    std::ifstream in( artifactPath, std::ios::binary );
    std::stringstream buffer;
    buffer << in.rdbuf();
    REQUIRE( buffer.str() == serializeRecipe( result.recipe ) );
  }
}

TEST_CASE( "Registry-resolved lab12 wrapper compiles its D3 source", "[equivalence]" )
{
  const LabSourceEntry *lab12 = nullptr;
  for ( const auto &e : shippedLabs() )
    if ( e.canonicalId == "lab12_sar_processing" )
      lab12 = &e;
  REQUIRE( lab12 != nullptr );
  REQUIRE( lab12->resolvedViaRegistry );
  REQUIRE( !lab12->document.steps.empty() );

  const CompileResult result = compileLabToRecipe( lab12->document, realCatalog() );
  REQUIRE( result.recipe["teaching_origin"]["lab_id"].asString() ==
           "lab12_sar_processing" );
  // Canonical id wins even though executable content is D3-sourced.
  REQUIRE( result.recipe["recipe_id"].asString() == "lab.lab12_sar_processing" );
  REQUIRE( result.recipe["stages"].size() ==
           lab12->document.steps.size() + lab12->document.questions.size() );
}

TEST_CASE( "Equivalence is asymmetric-proof: diffs are caught", "[equivalence]" )
{
  const LabDocument lab = loadLabOrFail( "lab02_spectral_analysis" );
  const CompileResult result = compileLabToRecipe( lab, realCatalog() );
  const Json::Value reference = loadJsonOrFail(
    fs::path( sourceDir() ) / "tests/data/scientific_recipes/reference" /
    "lab.lab02_spectral_analysis.json" );

  std::vector<std::string> diff;
  REQUIRE( recipesEquivalent( result.recipe, reference, &diff ) );

  // Each deliberate mutation must flip the verdict.
  // wrong stage kind
  {
    Json::Value broken = reference;
    broken["stages"][0]["kind"] = "operator";
    std::vector<std::string> d;
    REQUIRE( !recipesEquivalent( result.recipe, broken, &d ) );
  }
  // wrong params
  {
    Json::Value broken = reference;
    broken["stages"][2]["params"]["red"] = 6;
    std::vector<std::string> d;
    REQUIRE( !recipesEquivalent( result.recipe, broken, &d ) );
  }
  // dropped verifier hook
  {
    Json::Value broken = reference;
    Json::Value hooks( Json::arrayValue );
    for ( Json::ArrayIndex i = 1; i < broken["stages"][2]["verifier_hooks"].size(); ++i )
      hooks.append( broken["stages"][2]["verifier_hooks"][i] );
    broken["stages"][2]["verifier_hooks"] = hooks;
    std::vector<std::string> d;
    REQUIRE( !recipesEquivalent( result.recipe, broken, &d ) );
  }
  // changed reflection prompt
  {
    Json::Value broken = reference;
    broken["stages"][5]["prompt"] = "different question?";
    std::vector<std::string> d;
    REQUIRE( !recipesEquivalent( result.recipe, broken, &d ) );
  }
}
