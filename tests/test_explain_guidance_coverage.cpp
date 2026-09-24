// tests/test_explain_guidance_coverage.cpp
//
// RS14-15 Slice G coverage oracle: the authored guidance corpus on disk must
// stay in lockstep with the LIVE operator registry. Every corpus operator id
// must resolve in the registry; every authored parameter rationale must name
// a parameter that exists in the live schema; the corpus must cover at least
// fifteen operators across at least four operator families.
//
// Adversarial contract: any drift — a renamed operator, a renamed or removed
// parameter, a broken citation — fails HERE instead of degrading silently.
// Renaming one parameter in one corpus file must fail this suite.
//
// Teaching-agent consistency: for every corpus entry, a full builder pass
// (registry knowledge + guidance store) must produce an explanation that the
// hallucination-guard validator accepts — authored narrative never poses as
// machine fact, machine facts always cite machine evidence.
#include <catch2/catch_test_macros.hpp>

#include "explain/adapters/registry_operator_knowledge.h"
#include "explain/explanation_builder.h"
#include "explain/explanation_validator.h"
#include "explain/guidance_store.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"

#include <json/json.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#ifndef SICNU_EXPLAIN_GUIDANCE_DIR
#define SICNU_EXPLAIN_GUIDANCE_DIR "data/explain/guidance"
#endif

using namespace sicnu::explain;
using sicnu::explain::adapters::RegistryOperatorKnowledge;

namespace
{

// Corpus files are named after their operator id with the namespace colon
// mapped to an underscore ("rs:ndvi" → "rs_ndvi.json") so the tree stays
// Windows-checkout safe; no operator family prefix contains an underscore,
// so the mapping is unambiguous and verified against content below.
std::string operatorIdFromFileStem( const std::string &stem )
{
  const size_t colon = stem.find( '_' );
  if ( colon == std::string::npos )
    return stem;
  return stem.substr( 0, colon ) + ":" + stem.substr( colon + 1 );
}

std::set<std::string> entryOperatorIds( const GuidanceStore &store )
{
  std::set<std::string> ids;
  for ( const auto &entry : std::filesystem::directory_iterator( SICNU_EXPLAIN_GUIDANCE_DIR ) )
  {
    if ( entry.is_regular_file() && entry.path().extension() == ".json" )
      ids.insert( operatorIdFromFileStem( entry.path().stem().string() ) );
  }
  return ids;
}

} // namespace

TEST_CASE( "corpus covers at least fifteen live operators across four families",
           "[explain][guidance][coverage]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  const RegistryOperatorKnowledge knowledge( sicnu::operators::RSOperatorRegistry::instance() );
  std::vector<GuidanceLoadProblem> problems;
  const std::unique_ptr<GuidanceStore> store =
    GuidanceStore::loadFromDirectory( SICNU_EXPLAIN_GUIDANCE_DIR, problems );
  REQUIRE( store != nullptr );
  // The corpus itself must load clean: a committed corpus with broken files
  // is drift, not content.
  CHECK( store->loadProblems().empty() );
  CHECK( store->entryCount() >= 15 );

  const std::set<std::string> fileIds = entryOperatorIds( *store );
  CHECK( fileIds.size() == store->entryCount() ); // one entry per file, no dupes

  std::set<std::string> families;
  for ( const std::string &operatorId : fileIds )
  {
    const std::optional<OperatorFacts> facts = knowledge.findOperator( operatorId );
    if ( facts.has_value() )
      families.insert( facts->group );
  }
  CHECK( families.size() >= 4 );
}

TEST_CASE( "every corpus operator id and parameter name exists in the live registry",
           "[explain][guidance][coverage][drift]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  const RegistryOperatorKnowledge knowledge( sicnu::operators::RSOperatorRegistry::instance() );
  std::vector<GuidanceLoadProblem> problems;
  const std::unique_ptr<GuidanceStore> store =
    GuidanceStore::loadFromDirectory( SICNU_EXPLAIN_GUIDANCE_DIR, problems );
  REQUIRE( store != nullptr );

  const ExplanationValidator validator( knowledge, store.get() );

  std::vector<std::string> failures;
  for ( const std::string &operatorId : entryOperatorIds( *store ) )
  {
    const std::optional<StepGuidance> guidance = store->guidanceFor( operatorId, "" );
    if ( !guidance.has_value() )
    {
      failures.push_back( operatorId + ": file name does not match its operatorId content" );
      continue;
    }
    const std::optional<OperatorFacts> facts = knowledge.findOperator( guidance->operatorId );
    if ( !facts.has_value() )
    {
      failures.push_back( guidance->operatorId + ": operator not in the live registry" );
      continue;
    }
    for ( const AuthoredParameterRationale &rationale : guidance->parameterRationale )
    {
      bool known = false;
      for ( const ParamFact &param : facts->parameters )
        known = known || param.name == rationale.parameter;
      if ( !known )
        failures.push_back( guidance->operatorId + ": rationale parameter '" +
                            rationale.parameter + "' not in the live schema" );
    }
    // The validator is the second, independent drift gate (citations,
    // narrative tokens). It must accept every corpus entry.
    for ( const ValidationIssue &issue : validator.validateGuidance( *guidance ) )
      failures.push_back( guidance->operatorId + ": " + issue.code + " (" + issue.message + ")" );
  }
  CHECK( failures.empty() );
}

TEST_CASE( "builder output for every corpus entry passes the hallucination guard",
           "[explain][guidance][coverage][teaching]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  const RegistryOperatorKnowledge knowledge( sicnu::operators::RSOperatorRegistry::instance() );
  std::vector<GuidanceLoadProblem> problems;
  const std::unique_ptr<GuidanceStore> store =
    GuidanceStore::loadFromDirectory( SICNU_EXPLAIN_GUIDANCE_DIR, problems );
  REQUIRE( store != nullptr );

  const StepExplanationBuilder builder( knowledge, *store, nullptr );
  const ExplanationValidator validator( knowledge, store.get() );

  std::vector<std::string> failures;
  int built = 0;
  for ( const std::string &operatorId : entryOperatorIds( *store ) )
  {
    const std::optional<StepGuidance> guidance = store->guidanceFor( operatorId, "" );
    if ( !guidance.has_value() )
      continue;
    const ExplanationRequest request =
      makeOperatorRequest( WorkflowKindAgentPlan, "coverage-oracle", "step-1", operatorId,
                           guidance->operatorId );
    const BuildOutcome outcome = builder.build( request );
    if ( outcome.failed() )
    {
      failures.push_back( guidance->operatorId + ": build failed " + outcome.failureCode );
      continue;
    }
    ++built;
    const ValidationReport report = validator.validate( outcome.explanation );
    for ( const ValidationIssue &issue : report.issues )
      failures.push_back( guidance->operatorId + ": " + issue.code + " (" + issue.message + ")" );
  }
  CHECK( built >= 15 );
  CHECK( failures.empty() );
}
