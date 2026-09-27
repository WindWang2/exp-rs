// tests/test_harness_adversarial_matrix.cpp — Track 8 R4 WP-A: the model-
// authored plan layer of the LLM failure matrix, in the deterministic-
// injection tradition of test_model_failure_matrix.cpp.
//
// Failure classes covered here (PLAN.md mapping; corpus samples in
// tests/data/harness_adversarial_corpus.json):
//   1 hallucinated reference  — plan step names an operator outside the
//                               registries (whitelist = RSOperatorRegistry +
//                               AtomicAlgorithmRegistry, agent_plan.cpp:40).
//   2 format error            — top level is an array/scalar, not an object.
//   3 refusal / empty output  — model returns no steps.
//   8 self-contradiction      — one plan declares the same output name twice
//                               (two contradictory claims about one product).
// plus the reader's type-confusion contract: readAgentPlan's header promises
// "Returns false with a typed error" (never throws) — a hostile document
// must never crash the reader regardless of member types.
//
// Ground truth comes from these contracts, not from the implementation:
// - classes 1-3: existing documented behavior (agent_plan.h comments, the
//   INVALID_PLAN branch table in readAgentPlan/validateAgentPlan) — pinned
//   as adversarial regression anchors.
// - type-confusion, duplicate outputs, step bound: new contracts declared
//   HERE first (red), then implemented (green).

#include <catch2/catch_test_macros.hpp>

#include "adversarial_corpus.h"
#include "agent/harness/agent_plan.h"
#include "agent/harness/harness_error.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace {

using sicnu::agent::harness::AgentPlan;
using sicnu::agent::harness::AgentPlanIssue;
using sicnu::agent::harness::HarnessError;
using sicnu::agent::harness::readAgentPlan;
using sicnu::agent::harness::validateAgentPlan;
using sicnu::agent::harness::compilePlanToWorkflowJson;

Json::Value parse( const std::string &text )
{
  Json::Value doc;
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::CharReader *reader = builder.newCharReader();
  const bool ok = reader->parse( text.data(), text.data() + text.size(), &doc, &errors );
  delete reader;
  if ( !ok )
    return Json::Value( Json::nullValue );
  return doc;
}

/// A minimal plan that passes structural validation (single real operator).
Json::Value goodPlan( const std::string &outputName = "product" )
{
  return parse( R"({
    "kind": "execution_plan",
    "schema_version": "2.0",
    "plan_id": "adv-matrix",
    "goal": "adversarial matrix fixture",
    "intent": "ndvi",
    "inputs": [ { "name": "scene", "ref": "data/scene.tif" } ],
    "steps": [ { "id": "ndvi", "operator_id": "rs:spectral_index",
                 "params": { "index": "ndvi" },
                 "inputs": [], "verification": "raster" } ],
    "outputs": [ { "name": ")" + outputName + R"(", "from_step": "ndvi",
                   "port": "output", "kind": "raster" } ],
    "verification": { "enabled": true, "final_map": false }
  } )");
}

std::vector<std::string> issueSummaries( const std::vector<AgentPlanIssue> &issues )
{
  std::vector<std::string> summaries;
  for ( const AgentPlanIssue &issue : issues )
    summaries.push_back( issue.error.summary );
  return summaries;
}

bool anySummaryContains( const std::vector<AgentPlanIssue> &issues,
                         const std::string &needle )
{
  for ( const std::string &summary : issueSummaries( issues ) )
    if ( summary.find( needle ) != std::string::npos )
      return true;
  return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Class 1 — hallucinated reference (pin: documented whitelist contract)
// ---------------------------------------------------------------------------

TEST_CASE( "a hallucinated operator reference is a typed INVALID_PLAN with a "
           "search action, never a silent pass",
           "[harness][adversarial][plan]" )
{
  Json::Value doc = goodPlan();
  doc["steps"][0]["operator_id"] = "rs:fabricate_revenue"; // not in any registry

  AgentPlan plan;
  HarnessError error;
  REQUIRE( readAgentPlan( doc, plan, error ) ); // the READER accepts shape...

  const auto issues = validateAgentPlan( plan );
  REQUIRE_FALSE( issues.empty() );
  CHECK( anySummaryContains( issues, "Unknown operator id: rs:fabricate_revenue" ) );
  // The hallucination is repairable through capability search, not apology.
  CHECK( issues.front().repairable );

  // ...but the COMPILER refuses: hallucinated refs never reach the engine.
  HarnessError compileError;
  const std::string workflow = compilePlanToWorkflowJson( plan, compileError );
  CHECK( workflow.empty() );
  CHECK( compileError.code == sicnu::agent::harness::error_codes::kInvalidPlan );
}

TEST_CASE( "a hallucinated file reference stays verifiable: an unknown input "
           "path cannot be waved through as a known dataset",
           "[harness][adversarial][plan]" )
{
  // Contract (preflightIntent docs): refs resolve at preflight; the plan
  // layer must NOT fabricate resolution — inputs are carried verbatim and
  // the fingerprint changes when the reference changes (evidence identity).
  const Json::Value a = goodPlan();
  Json::Value b = goodPlan();
  b["inputs"][0]["ref"] = "data/scene_that_does_not_exist.tif";

  AgentPlan planA, planB;
  HarnessError error;
  REQUIRE( readAgentPlan( a, planA, error ) );
  REQUIRE( readAgentPlan( b, planB, error ) );
  // Different references → different fingerprints: a hallucinated path can
  // never alias a real dataset's evidence trail.
  CHECK( sicnu::agent::harness::planFingerprint( planA ) !=
         sicnu::agent::harness::planFingerprint( planB ) );
}

// ---------------------------------------------------------------------------
// Class 2 — format error (pin: structured rejection of non-object plans)
// ---------------------------------------------------------------------------

TEST_CASE( "a plan whose top level is not an object is rejected structurally",
           "[harness][adversarial][plan]" )
{
  for ( const char *text : { "[]", "\"a string\"", "42", "null", "true" } )
  {
    INFO( "top level: " << text );
    AgentPlan plan;
    HarnessError error;
    REQUIRE_NOTHROW( readAgentPlan( parse( text ), plan, error ) );
    CHECK_FALSE( readAgentPlan( parse( text ), plan, error ) );
    CHECK( error.code == sicnu::agent::harness::error_codes::kInvalidPlan );
  }
}

// ---------------------------------------------------------------------------
// Class 3 — refusal / empty output (pin: no steps is a typed refusal)
// ---------------------------------------------------------------------------

TEST_CASE( "an empty or absent steps array is a typed refusal with the plan "
           "tool as the suggested action",
           "[harness][adversarial][plan]" )
{
  // The model "refuses" by producing an envelope with nothing in it.
  for ( const char *text : { R"({"kind":"execution_plan","steps":[]})", "{}" } )
  {
    INFO( "doc: " << text );
    AgentPlan plan;
    HarnessError error;
    REQUIRE_FALSE( readAgentPlan( parse( text ), plan, error ) );
    CHECK( error.code == sicnu::agent::harness::error_codes::kInvalidPlan );
    CHECK( error.summary.find( "no steps" ) != std::string::npos );
    // Recovery points at the plan tool, not at blind retry.
    bool hasPlanAction = false;
    for ( const Json::Value &action : error.suggestedActions )
      if ( action.isObject() && action["action"].asString() == "harness:plan" )
        hasPlanAction = true;
    CHECK( hasPlanAction );
  }
}

// ---------------------------------------------------------------------------
// NEW CONTRACT (red → green): type confusion must never crash the reader.
// readAgentPlan promises "false + typed error, never throws"; the current
// `.asString()` calls throw Json::LogicError on non-string members.
// ---------------------------------------------------------------------------

TEST_CASE( "type-confused discriminator fields are rejected, not crashed on",
           "[harness][adversarial][plan][types]" )
{
  // Discriminators (kind / schema_version) decide WHICH document this is;
  // a non-string there is a structural lie → typed rejection.
  Json::Value kindIsArray = goodPlan();
  kindIsArray["kind"] = Json::Value( Json::arrayValue );

  Json::Value versionIsObject = goodPlan();
  versionIsObject["schema_version"] = Json::Value( Json::objectValue );

  for ( const Json::Value &doc : { kindIsArray, versionIsObject } )
  {
    AgentPlan plan;
    HarnessError error;
    bool threw = false;
    bool accepted = false;
    try
    {
      accepted = readAgentPlan( doc, plan, error );
    }
    catch ( ... )
    {
      threw = true;
    }
    CHECK_FALSE( threw );          // the header contract: never throws
    CHECK_FALSE( accepted );       // and a lying discriminator never plans
    CHECK( error.code == sicnu::agent::harness::error_codes::kInvalidPlan );
  }
}

TEST_CASE( "type-confused content fields degrade to defaults instead of "
           "throwing", "[harness][adversarial][plan][types]" )
{
  // Content fields (goal/plan_id/intent) are not discriminators: the reader
  // tolerates a wrong type by falling back to the documented default and
  // lets structural validation speak. Any throw is a contract violation.
  Json::Value doc = goodPlan();
  doc["goal"] = 42; // number where a string belongs
  doc["plan_id"] = Json::Value( Json::arrayValue );
  doc["intent"] = true;

  AgentPlan plan;
  HarnessError error;
  bool threw = false;
  try
  {
    REQUIRE( readAgentPlan( doc, plan, error ) );
  }
  catch ( ... )
  {
    threw = true;
  }
  CHECK_FALSE( threw );
  // Non-string intent falls back to "" (custom plan), not to a crash and
  // not to a fabricated known intent.
  CHECK( plan.intent.empty() );
}

TEST_CASE( "type-confused step fields produce validation issues, never "
           "exceptions", "[harness][adversarial][plan][types]" )
{
  Json::Value doc = goodPlan();
  doc["steps"][0]["verification"] = Json::Value( Json::objectValue ); // string vocab
  doc["steps"][0]["role"] = 7;                                        // string vocab
  doc["steps"][0]["title"] = Json::Value( Json::arrayValue );         // compile-time
  doc["outputs"][0]["from_step"] = 3;                                 // wiring check

  AgentPlan plan;
  HarnessError error;
  REQUIRE( readAgentPlan( doc, plan, error ) );

  bool threw = false;
  std::vector<AgentPlanIssue> issues;
  HarnessError compileError;
  try
  {
    issues = validateAgentPlan( plan );
    const std::string workflow = compilePlanToWorkflowJson( plan, compileError );
    CHECK( workflow.empty() ); // type-confused plan never compiles clean
  }
  catch ( ... )
  {
    threw = true;
  }
  CHECK_FALSE( threw );
}

// ---------------------------------------------------------------------------
// Class 8 — self-contradiction (new contract, red → green): one output name,
// two producer claims inside one plan.
// ---------------------------------------------------------------------------

TEST_CASE( "a self-contradictory plan with duplicate output names is flagged "
           "as a consistency issue", "[harness][adversarial][plan][consistency]" )
{
  Json::Value doc = goodPlan();
  // Two contradictory claims about the same product: both steps claim to
  // produce "product". Whatever the engine does with two writers to one
  // artifact, the plan layer must surface the contradiction — evidence and
  // verification are keyed by output identity, so two claims poison both.
  Json::Value second = parse( R"({
    "id": "ndvi_again", "operator_id": "rs:spectral_index",
    "params": { "index": "ndvi" }, "inputs": [], "verification": "raster"
  } )" );
  doc["steps"].append( second );
  Json::Value secondOutput = parse( R"({
    "name": "product", "from_step": "ndvi_again", "port": "output", "kind": "raster"
  } )" );
  doc["outputs"].append( secondOutput );

  AgentPlan plan;
  HarnessError error;
  REQUIRE( readAgentPlan( doc, plan, error ) );

  const auto issues = validateAgentPlan( plan );
  CHECK( anySummaryContains( issues, "Duplicate output name" ) );
  // And the compiler refuses to hand the engine a contradictory plan.
  HarnessError compileError;
  const std::string workflow = compilePlanToWorkflowJson( plan, compileError );
  CHECK( workflow.empty() );
  CHECK( compileError.code == sicnu::agent::harness::error_codes::kInvalidPlan );
}

TEST_CASE( "distinct output names with distinct producers stay consistent",
           "[harness][adversarial][plan][consistency]" )
{
  Json::Value doc = goodPlan();
  Json::Value second = parse( R"({
    "id": "thumb", "operator_id": "rs:spectral_index",
    "params": { "index": "ndvi" }, "inputs": [], "verification": "skip"
  } )" );
  doc["steps"].append( second );
  Json::Value secondOutput = parse( R"({
    "name": "thumbnail", "from_step": "thumb", "port": "output", "kind": "raster"
  } )" );
  doc["outputs"].append( secondOutput );

  AgentPlan plan;
  HarnessError error;
  REQUIRE( readAgentPlan( doc, plan, error ) );
  // The control plan: two outputs, two producers, zero consistency issues —
  // the duplicate-output probe fires on contradiction, not on plurality.
  CHECK_FALSE( anySummaryContains( validateAgentPlan( plan ), "Duplicate output name" ) );
}

// ---------------------------------------------------------------------------
// NEW CONTRACT (red → green): a plan is bounded. A runaway model cannot make
// the compiler allocate without limit. Anchor: HarnessSessionStore's
// kMaxDocumentBytes discipline; bound value is a documented drift anchor.
// ---------------------------------------------------------------------------

TEST_CASE( "a plan beyond the step bound is a typed rejection before "
           "validation walks it", "[harness][adversarial][plan][bounds]" )
{
  Json::Value doc = goodPlan();
  Json::Value runawayStep = parse( R"({
    "id": "x", "operator_id": "rs:spectral_index", "inputs": []
  } )" );
  for ( int i = 0; i < 5000; ++i )
  {
    runawayStep["id"] = "step-" + std::to_string( i );
    doc["steps"].append( runawayStep );
  }

  AgentPlan plan;
  HarnessError error;
  bool threw = false;
  bool accepted = false;
  try
  {
    accepted = readAgentPlan( doc, plan, error );
  }
  catch ( ... )
  {
    threw = true;
  }
  CHECK_FALSE( threw );
  CHECK_FALSE( accepted );
  CHECK( error.code == sicnu::agent::harness::error_codes::kInvalidPlan );
}

// ---------------------------------------------------------------------------
// WP-G: corpus-driven cases. The corpus (tests/data/harness_adversarial_
// corpus.json) is the single sample source shared with the SSE lane; the
// load itself schema-validates the corpus (drift = failed load = red test).
// ---------------------------------------------------------------------------

TEST_CASE( "the adversarial corpus loads and satisfies its own schema",
           "[harness][adversarial][corpus]" )
{
  sicnu::testing::AdversarialCorpus corpus;
  std::string error;
  REQUIRE( sicnu::testing::AdversarialCorpus::load(
    sicnu::testing::adversarialCorpusPath(), corpus, &error ) );
  CHECK( error.empty() );
  CHECK( corpus.samples().size() >= 8 );
}

TEST_CASE( "corpus plan-reader samples behave exactly as their recorded "
           "contract says", "[harness][adversarial][corpus][plan]" )
{
  sicnu::testing::AdversarialCorpus corpus;
  std::string error;
  REQUIRE( sicnu::testing::AdversarialCorpus::load(
    sicnu::testing::adversarialCorpusPath(), corpus, &error ) );

  const auto samples = corpus.forSeam( "agent_plan.read" );
  REQUIRE_FALSE( samples.empty() );
  for ( const sicnu::testing::AdversarialSample &sample : samples )
  {
    INFO( "corpus sample: " << sample.id );
    AgentPlan plan;
    HarnessError planError;
    const bool accepted = readAgentPlan( sample.payload, plan, planError );
    if ( sample.expected["outcome"].asString() == "reject" )
    {
      CHECK_FALSE( accepted );
      CHECK( planError.code == sample.expected["code"].asString() );
    }
    else
    {
      CHECK( accepted );
    }
  }
}

TEST_CASE( "corpus plan-validator samples are rejected with the recorded "
           "summary semantics", "[harness][adversarial][corpus][plan]" )
{
  sicnu::testing::AdversarialCorpus corpus;
  std::string error;
  REQUIRE( sicnu::testing::AdversarialCorpus::load(
    sicnu::testing::adversarialCorpusPath(), corpus, &error ) );

  const auto samples = corpus.forSeam( "agent_plan.validate" );
  REQUIRE_FALSE( samples.empty() );
  for ( const sicnu::testing::AdversarialSample &sample : samples )
  {
    INFO( "corpus sample: " << sample.id );
    AgentPlan plan;
    HarnessError planError;
    REQUIRE( readAgentPlan( sample.payload, plan, planError ) );

    const auto issues = validateAgentPlan( plan );
    CHECK_FALSE( issues.empty() );
    HarnessError compileError;
    CHECK( compilePlanToWorkflowJson( plan, compileError ).empty() );
    if ( sample.expected.isMember( "summary_contains" ) )
    {
      bool found = false;
      for ( const AgentPlanIssue &issue : issues )
        if ( issue.error.summary.find(
               sample.expected["summary_contains"].asString() ) != std::string::npos )
          found = true;
      CHECK( found );
    }
  }
}
