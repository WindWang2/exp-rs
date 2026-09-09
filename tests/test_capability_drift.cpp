// tests/test_capability_drift.cpp
//
// Harness 7.0 capability-knowledge drift guard (mission Area A).
//
// The knowledge entries in data/agent/capabilities/*.json are a *derivation
// target*, not a second schema source. These tests pin the layer to the live
// code so the two cannot silently diverge:
//   1. the directory loads clean (no skipped/invalid entries),
//   2. every entry id resolves in the live operator registry,
//   3. every declared intent is in the closed plan vocabulary,
//   4. declared modality agrees with operator-declared x-rs-contract facts,
//   5. every intent in the closed vocabulary is served by >= 1 entry
//      (coverage floor — a new intent without knowledge is a drift),
//   6. variant resolution is deterministic and pinned for the flagship
//      spectral_index operator.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/harness/agent_plan.h"
#include "agent/harness/capability_graph.h"
#include "agent/harness/capability_knowledge.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/recipe_catalog.h"
#include "agent/harness/scientific_preflight.h"

#include <operators/framework/rs_operator.h>
#include <operators/framework/rs_operator_registry.h>

#include <fstream>

#include <QDir>
#include <QTemporaryDir>

#include <set>
#include <string>
#include <vector>

using sicnu::agent::harness::CapabilityKnowledge;
using sicnu::operators::RSOperatorRegistry;

namespace {

/// The closed intent vocabulary of agent_plan.cpp, pinned here so adding an
/// intent without capability-knowledge coverage fails this suite.
const std::vector<std::string> kAllIntents = {
  "ndvi", "change", "sar_change", "classify", "phenology",
  "evi", "savi", "ndre", "ndwi", "mndwi", "ndsi", "nbr", "dnbr", "ndbi", "bsi",
  "water", "flood", "sar_water", "sar_flood", "sar", "ship",
  "temporal", "terrain", "accuracy", "qa", "preprocess", "inference",
};

} // namespace

TEST_CASE( "capability knowledge loads clean", "[harness][capability][drift]" )
{
  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
  const int loaded = knowledge.reload();
  INFO( "problems: " << [&] {
    std::string joined;
    for ( const std::string &problem : knowledge.loadProblems() )
      joined += problem + "; ";
    return joined;
  }() );
  REQUIRE( knowledge.loadProblems().empty() );
  REQUIRE( loaded > 0 );
}

TEST_CASE( "capability entry ids resolve in the operator registry",
           "[harness][capability][drift]" )
{
  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
  knowledge.reload();

  std::vector<std::string> unresolved;
  for ( const std::string &id : knowledge.entryIds() )
  {
    if ( !RSOperatorRegistry::instance().create( id ) )
      unresolved.push_back( id );
  }
  INFO( "unresolved ids: " << [&] {
    std::string joined;
    for ( const std::string &id : unresolved )
      joined += id + "; ";
    return joined;
  }() );
  REQUIRE( unresolved.empty() );
}

TEST_CASE( "capability intents are in the closed plan vocabulary",
           "[harness][capability][drift]" )
{
  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
  knowledge.reload();

  for ( const std::string &id : knowledge.entryIds() )
  {
    const Json::Value entry = knowledge.entryForOperator( id );
    for ( const Json::Value &intent : entry.get( "intents", Json::Value( Json::arrayValue ) ) )
    {
      INFO( "entry " << id << " declares intent " << intent.asString() );
      REQUIRE( sicnu::agent::harness::isKnownIntent( intent.asString() ) );
    }
  }
}

TEST_CASE( "capability modality agrees with operator x-rs-contract facts",
           "[harness][capability][drift]" )
{
  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
  knowledge.reload();

  // Operators that declare metadata()["x-rs-contract"]["modality"] are the
  // authoritative source for that fact (they own their physics); the
  // knowledge layer must not contradict them.
  for ( const std::string &id : knowledge.entryIds() )
  {
    const std::unique_ptr<sicnu::operators::RSOperator> op =
      RSOperatorRegistry::instance().create( id );
    if ( !op )
      continue;
    const Json::Value contract = op->metadata().get( "x-rs-contract", Json::Value() );
    if ( !contract.isObject() || !contract.isMember( "modality" ) )
      continue;
    const std::string declared = contract["modality"].asString();
    const Json::Value entry = knowledge.entryForOperator( id );
    bool agrees = false;
    for ( const Json::Value &modality : entry.get( "modality", Json::Value( Json::arrayValue ) ) )
    {
      if ( modality.isString() && modality.asString() == declared )
        agrees = true;
    }
    INFO( "entry " << id << " modality list disagrees with operator contract modality "
                   << declared );
    REQUIRE( agrees );
  }
}

TEST_CASE( "every plan intent is served by at least one capability entry",
           "[harness][capability][drift]" )
{
  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
  knowledge.reload();

  std::vector<std::string> unserved;
  for ( const std::string &intent : kAllIntents )
  {
    if ( knowledge.operatorsForIntent( intent ).empty() )
      unserved.push_back( intent );
  }
  INFO( "unserved intents: " << [&] {
    std::string joined;
    for ( const std::string &intent : unserved )
      joined += intent + "; ";
    return joined;
  }() );
  REQUIRE( unserved.empty() );
}

TEST_CASE( "spectral_index variant resolution is deterministic and pinned",
           "[harness][capability][drift]" )
{
  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
  knowledge.reload();

  // Pinned NDVI facts (the flagship index).
  Json::Value params;
  params["index"] = "NDVI";
  const Json::Value ndvi = knowledge.entryForOperator( "rs:spectral_index", params );
  REQUIRE( ndvi.get( "band_roles", Json::Value() ).get( "red", 0 ).asInt() == 1 );
  REQUIRE( ndvi.get( "band_roles", Json::Value() ).get( "nir", 0 ).asInt() == 1 );
  bool servesNdvi = false;
  for ( const Json::Value &intent : ndvi.get( "intents", Json::Value( Json::arrayValue ) ) )
    servesNdvi |= intent.asString() == "ndvi";
  REQUIRE( servesNdvi );

  // MNDWI swaps NIR for SWIR — the variant must not leak NDVI roles.
  params["index"] = "MNDWI";
  const Json::Value mndwi = knowledge.entryForOperator( "rs:spectral_index", params );
  REQUIRE( mndwi.get( "band_roles", Json::Value() ).get( "green", 0 ).asInt() == 1 );
  REQUIRE( mndwi.get( "band_roles", Json::Value() ).get( "swir", 0 ).asInt() == 1 );
  REQUIRE( mndwi.get( "band_roles", Json::Value() ).get( "nir", 0 ).asInt() == 0 );

  // Family defaults flow through extends: the merged NDVI entry inherits the
  // spectral_index family verification contract.
  const Json::Value checks = ndvi.get( "verification", Json::Value() ).get(
    "checks", Json::Value( Json::arrayValue ) );
  REQUIRE( checks.size() >= 4 );
  REQUIRE( ndvi.get( "verification", Json::Value() ).get( "expected_kind", "" ).asString()
           == "raster" );

  // Atomic pinned entries agree with their variants (rs:ndvi vs the
  // spectral_index NDVI variant).
  const Json::Value atomicNdvi = knowledge.entryForOperator( "rs:ndvi" );
  REQUIRE( atomicNdvi.get( "band_roles", Json::Value() ).get( "red", 0 ).asInt() == 1 );
  REQUIRE( atomicNdvi.get( "band_roles", Json::Value() ).get( "nir", 0 ).asInt() == 1 );

  // Unknown operator ids answer null — never a guessed entry.
  REQUIRE( knowledge.entryForOperator( "rs:definitely_not_registered" ).isNull() );

  // SAR family requirements surface through the SAR entries.
  const Json::Value sarChange = knowledge.entryForOperator( "rs:sar_change" );
  REQUIRE( sarChange.get( "temporal", Json::Value() ).get( "min_scenes", 0 ).asInt() == 2 );
  const Json::Value sar = knowledge.familyDefault( "sar" );
  REQUIRE( sar.get( "modality", Json::Value( Json::arrayValue ) )[0].asString() == "sar" );
}

TEST_CASE( "validateEntry rejects malformed entries", "[harness][capability][drift]" )
{
  Json::Value entry;
  entry["id"] = "rs:nonexistent";
  entry["family"] = "spectral_index";
  entry["bogus_key"] = true;
  entry["band_roles"]["ultraviolet"] = 1;
  entry["intents"].append( "not_an_intent" );
  const std::vector<std::string> problems = CapabilityKnowledge::validateEntry( entry );
  REQUIRE( problems.size() >= 3 );

  Json::Value valid;
  valid["id"] = "rs:ndvi";
  valid["family"] = "spectral_index";
  valid["intents"].append( "ndvi" );
  valid["band_roles"]["red"] = 1;
  valid["band_roles"]["nir"] = 1;
  REQUIRE( CapabilityKnowledge::validateEntry( valid ).empty() );
}

// ---------------------------------------------------------------------------
// Intent → capability graph (Harness 7.0 Area B)
// ---------------------------------------------------------------------------

TEST_CASE( "goal intent classification is deterministic and typed",
           "[harness][capability][graph]" )
{
  using sicnu::agent::harness::IntentResolution;
  using sicnu::agent::harness::resolveGoalIntent;

  // Clear evidence resolves.
  const IntentResolution ndvi = resolveGoalIntent( "Compute the NDVI of the active scene" );
  REQUIRE( ndvi.status == "resolved" );
  REQUIRE( ndvi.intent == "ndvi" );

  // SAR + flood compounds into sar_flood rather than tying with flood.
  const IntentResolution sarFlood = resolveGoalIntent( "SAR 洪水范围制图" );
  REQUIRE( sarFlood.status == "resolved" );
  REQUIRE( sarFlood.intent == "sar_flood" );

  // Equal evidence for two intents is typed ambiguity with candidates —
  // never a guess.
  const IntentResolution tied = resolveGoalIntent( "water change" );
  REQUIRE( tied.status == "ambiguous" );
  REQUIRE( tied.intent.empty() );
  REQUIRE( tied.candidates.size() >= 2 );
  REQUIRE( !tied.ambiguityError()["details"].isNull() );

  // No evidence at all is typed unresolved.
  const IntentResolution nothing = resolveGoalIntent( "hello world" );
  REQUIRE( nothing.status == "unresolved" );
  REQUIRE( nothing.intent.empty() );

  // Determinism: identical input, identical output.
  const IntentResolution again = resolveGoalIntent( "water change" );
  REQUIRE( again.status == tied.status );
  REQUIRE( again.candidates.size() == tied.candidates.size() );
}

TEST_CASE( "feasibility uses dataset facts, not guesses",
           "[harness][capability][graph]" )
{
  using sicnu::agent::harness::CapabilityKnowledge;
  using sicnu::agent::harness::capabilityCandidates;
  using sicnu::agent::harness::evaluateFeasibility;

  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
  knowledge.reload();

  Json::Value optical;  // 4-band optical scene with roles
  optical["modality"] = "optical";
  optical["band_count"] = 4;
  optical["bands"][0]["role"] = "blue";
  optical["bands"][1]["role"] = "green";
  optical["bands"][2]["role"] = "red";
  optical["bands"][3]["role"] = "nir";
  optical["radiometric_state"] = "surface_reflectance";

  const Json::Value ndviEntry = knowledge.entryForOperator( "rs:ndvi" );
  const Json::Value feasible = evaluateFeasibility( ndviEntry, optical );
  REQUIRE( feasible["feasible"].asBool() );

  // SWIR demand against the same 4-band scene blocks with a typed code.
  const Json::Value ndwiEntry = knowledge.entryForOperator( "rs:mndwi" );
  const Json::Value blocked = evaluateFeasibility( ndwiEntry, optical );
  REQUIRE( !blocked["feasible"].asBool() );
  REQUIRE( blocked["why_not"][0]["code"].asString() == "BAND_ROLE_UNRESOLVED" );

  // A SAR dataset is modality-infeasible for an optical-only capability.
  Json::Value sar;
  sar["modality"] = "sar";
  sar["band_count"] = 1;
  sar["bands"][0]["role"] = "vv";
  sar["radiometric_state"] = "gamma0";
  const Json::Value modalityBlocked = evaluateFeasibility( ndviEntry, sar );
  REQUIRE( !modalityBlocked["feasible"].asBool() );
  REQUIRE( modalityBlocked["why_not"][0]["code"].asString() == "MODALITY_MISMATCH" );

  // No facts at all degrades to a warning, not a fake pass.
  const Json::Value blind = evaluateFeasibility( ndviEntry, Json::Value() );
  REQUIRE( blind["feasible"].asBool() );
  REQUIRE( blind["score"].asDouble() < 1.0 );

  // Ranked candidates: NDVI intent over the optical scene lists capabilities
  // and puts feasible ones first.
  const Json::Value candidates = capabilityCandidates( "ndvi", optical );
  REQUIRE( candidates["total"].asInt() >= 1 );
  REQUIRE( candidates["candidates"][0]["feasible"].asBool() );

  // Unknown intents are rejected with a typed error, not an empty guess.
  const Json::Value unknown = capabilityCandidates( "teleportation", optical );
  REQUIRE( unknown["error"]["code"].asString() == "INVALID_PARAMETER" );
}

TEST_CASE( "intent requirements agree with the capability knowledge layer",
           "[harness][capability][drift]" )
{
  using sicnu::agent::harness::intentRequirements;
  using sicnu::agent::harness::isKnownIntent;

  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
  knowledge.reload();

  // The machine-readable preflight table must exist for every intent and its
  // band demands must be satisfiable by the knowledge entries serving that
  // intent. Index intents (ndvi..bsi) pin the requirements exactly — their
  // physics is the formula; science intents only demand coverage.
  static const std::set<std::string> kIndexIntents = {
    "ndvi", "evi", "savi", "ndre", "ndwi", "mndwi", "ndsi", "nbr", "dnbr", "ndbi", "bsi",
  };

  for ( const std::string &intent : kAllIntents )
  {
    INFO( "intent: " << intent );
    REQUIRE( isKnownIntent( intent ) );
    const Json::Value requirements = intentRequirements( intent );
    REQUIRE( requirements.isObject() );
    REQUIRE( requirements["intent"].asString() == intent );

    const Json::Value &requiredBands = requirements["band_roles"];
    const std::vector<std::string> serving =
      knowledge.operatorsForIntent( intent );
    REQUIRE_FALSE( serving.empty() );

    bool bandsCovered = requiredBands.empty();
    bool bandsExact = requiredBands.empty();
    bool scenesCovered = !requirements.isMember( "min_scenes" );
    for ( const std::string &operatorId : serving )
    {
      // Band-role candidate sets: the merged entry plus every variant's
      // roles (parameter-conditional capabilities serve intents via their
      // variants, e.g. spectral_index + index=BSI).
      std::vector<Json::Value> roleSets;
      const Json::Value merged = knowledge.entryForOperator( operatorId );
      if ( merged.get( "band_roles", Json::Value() ).isObject() )
        roleSets.push_back( merged["band_roles"] );
      for ( const Json::Value &variant :
            knowledge.rawEntry( operatorId ).get( "variants", Json::Value( Json::arrayValue ) ) )
        if ( variant.get( "band_roles", Json::Value() ).isObject() )
          roleSets.push_back( variant["band_roles"] );

      for ( const Json::Value &entryBands : roleSets )
      {
        bool superset = true;
        for ( const std::string &role : requiredBands.getMemberNames() )
          if ( !entryBands.isMember( role ) || entryBands[role].asInt() <
                                                  requiredBands[role].asInt() )
            superset = false;
        if ( superset )
          bandsCovered = true;
        bool same = superset;
        if ( same )
        {
          for ( const std::string &role : entryBands.getMemberNames() )
            if ( !requiredBands.isMember( role ) ||
                 requiredBands[role].asInt() > entryBands[role].asInt() )
              same = false;
        }
        if ( same )
          bandsExact = true;
      }
      const int entryMinScenes =
        merged.get( "temporal", Json::Value() ).get( "min_scenes", 0 ).asInt();
      if ( requirements.isMember( "min_scenes" ) &&
           entryMinScenes == requirements["min_scenes"].asInt() )
        scenesCovered = true;
    }

    if ( kIndexIntents.count( intent ) )
    {
      INFO( "index intent " << intent << " needs an entry whose band_roles match exactly" );
      REQUIRE( bandsExact );
    }
    else
    {
      INFO( "science intent " << intent << " needs an entry covering the preflight bands" );
      REQUIRE( bandsCovered );
    }
    if ( requirements.isMember( "min_scenes" ) )
    {
      INFO( "intent " << intent << " needs an entry declaring the same min_scenes" );
      REQUIRE( scenesCovered );
    }
    // The pair helper and the table must agree (single-source pin).
    REQUIRE( sicnu::agent::harness::intentRequiresPair( intent ) ==
             requirements["requires_pair"].asBool() );
  }

  // Unknown intents have no requirements document at all.
  REQUIRE( intentRequirements( "warp_speed" ).isNull() );
}

// ---------------------------------------------------------------------------
// Recipe catalog de-duplication (Harness 7.0 Area G)
// ---------------------------------------------------------------------------

TEST_CASE( "deleted near-clone recipe ids resolve through aliases",
           "[harness][recipes][dedup]" )
{
  using sicnu::agent::harness::RecipeCatalog;
  RecipeCatalog &catalog = RecipeCatalog::instance();
  catalog.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/recipes" );
  REQUIRE( catalog.reload() > 0 );
  INFO( "problems: " << [&] {
    std::string joined;
    for ( const std::string &problem : catalog.loadProblems() )
      joined += problem + "; ";
    return joined;
  }() );
  REQUIRE( catalog.loadProblems().empty() );

  // A deleted Landsat twin resolves to its canonical document.
  const Json::Value viaAlias = catalog.recipe( "harness.optical_ndvi_landsat" );
  REQUIRE_FALSE( viaAlias.isNull() );
  REQUIRE( viaAlias["recipe_id"].asString() == "harness.optical_ndvi" );
  REQUIRE_FALSE( catalog.recipe( "harness.classify_kmeans_coastal" ).isNull() );
  REQUIRE_FALSE( catalog.recipe( "harness.terrain_hillshade" ).isNull() );

  // Aliases never surface as separate catalog entries.
  for ( const Json::Value &summary : catalog.listRecipes() )
    REQUIRE( summary["recipe_id"].asString() != "harness.optical_ndvi_landsat" );
}

TEST_CASE( "recipe presets reproduce the collapsed variants",
           "[harness][recipes][dedup]" )
{
  using sicnu::agent::harness::RecipeCatalog;
  RecipeCatalog &catalog = RecipeCatalog::instance();
  catalog.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/recipes" );
  REQUIRE( catalog.reload() > 0 );

  // A resolvable (existing, though empty) dataset reference: instantiation
  // resolves paths, it never opens the raster.
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const std::string fixture =
    ( QDir( dir.path() ).filePath( QStringLiteral( "preset_fixture.tif" ) ) ).toStdString();
  {
    std::ofstream stream( fixture, std::ios::binary );
    stream << "synthetic";
  }

  auto instantiate = [ & ]( const std::string &preset ) {
    Json::Value bindings( Json::objectValue );
    bindings["slots"]["primary"] = fixture;
    if ( !preset.empty() )
      bindings["preset"] = preset;
    sicnu::agent::harness::HarnessError error;
    return catalog.instantiateRecipe( "harness.optical_ndvi", bindings, error );
  };

  // Base instantiation: no scale override (operator default 1.0 applies).
  const Json::Value basePlan = instantiate( "" );
  REQUIRE_FALSE( basePlan.isNull() );
  double baseScale = 1.0;
  for ( const Json::Value &step : basePlan["steps"] )
    if ( step["id"].asString() == "index" )
      baseScale = step["params"].get( "scale", 1.0 ).asDouble();
  REQUIRE( baseScale == Catch::Approx( 1.0 ) );

  // Landsat preset: the collapsed twin's scale override.
  const Json::Value landsatPlan = instantiate( "landsat" );
  REQUIRE_FALSE( landsatPlan.isNull() );
  double landsatScale = 1.0;
  for ( const Json::Value &step : landsatPlan["steps"] )
    if ( step["id"].asString() == "index" )
      landsatScale = step["params"].get( "scale", 1.0 ).asDouble();
  REQUIRE( landsatScale == Catch::Approx( 0.0001 ) );

  // Unknown presets fail typed and recoverably.
  sicnu::agent::harness::HarnessError error;
  Json::Value bindings( Json::objectValue );
  bindings["slots"]["primary"] = fixture;
  bindings["preset"] = "no_such_preset";
  const Json::Value plan =
    catalog.instantiateRecipe( "harness.optical_ndvi", bindings, error );
  CHECK( plan.isNull() );
  CHECK( error.code == sicnu::agent::harness::error_codes::kInvalidParameter );
}

TEST_CASE( "recipe expected_artifacts mirror declared outputs",
           "[harness][recipes][dedup]" )
{
  using sicnu::agent::harness::RecipeCatalog;
  RecipeCatalog &catalog = RecipeCatalog::instance();
  catalog.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/recipes" );
  REQUIRE( catalog.reload() > 0 );

  for ( const Json::Value &summary : catalog.listRecipes() )
  {
    const Json::Value doc = catalog.recipe( summary["recipe_id"].asString() );
    const Json::Value &artifacts = doc.get( "expected_artifacts", Json::Value() );
    if ( !artifacts.isArray() )
      continue;
    std::set<std::string> outputNames;
    for ( const Json::Value &output : doc.get( "outputs", Json::Value( Json::arrayValue ) ) )
      outputNames.insert( output.get( "name", "" ).asString() );
    for ( const Json::Value &artifact : artifacts )
    {
      INFO( "recipe " << summary["recipe_id"].asString() );
      REQUIRE( outputNames.count( artifact["name"].asString() ) == 1 );
    }
  }
}
