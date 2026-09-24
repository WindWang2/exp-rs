// tests/test_explain_registry_adapter.cpp
//
// RS14-15 Explainable Workflow (Slice B): the live registry knowledge
// adapter. `RegistryOperatorKnowledge` projects the REAL RSOperatorRegistry
// (builtin rs:* operators) onto the explain layer's IOperatorKnowledge
// interface. Facts must come from the live schema — parameter names, types,
// defaults, enums and ranges are read off `RSOperator::schema()` — and every
// deviation between the live schema surface and the projection contract is a
// typed drift record, never silently dropped or invented.
//
// Adversarial contract: an operator absent from the registry resolves to
// nullopt (fail-closed); a hostile schema surface degrades to typed drift
// records; unavailable stays unavailable (no placeholder success).
#include <catch2/catch_test_macros.hpp>

#include "explain/adapters/registry_operator_knowledge.h"
#include "explain/explanation_sources.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_operators_init.h"

#include <json/json.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace sicnu::explain;
using sicnu::explain::adapters::RegistryOperatorKnowledge;
using sicnu::operators::RSOperator;
using sicnu::operators::RSOperatorRegistry;

namespace
{

// A deliberately hostile operator: its schema() deviates from the surface
// every well-formed builtin produces. Used to prove the drift gate fires.
class DriftyOperator final : public RSOperator
{
public:
  std::string name() const override { return "test:drifty"; }
  std::string displayName() const override { return "Drifty"; }
  std::string description() const override { return "schema drift probe"; }
  Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context )
    override
  {
    (void)params;
    (void)context;
    return Json::Value( Json::objectValue );
  }

  Json::Value schema() const override
  {
    Json::Value root( Json::objectValue );
    root["title"] = "Drifty";
    root["type"] = "object";
    // properties is an ARRAY here (builtins emit an object keyed by name).
    Json::Value props( Json::arrayValue );
    Json::Value broken( Json::objectValue );
    broken["type"] = 42; // wrong-typed "type", and no "name" at all
    props.append( broken );
    Json::Value weirdType( Json::objectValue );
    weirdType["name"] = "weird";
    weirdType["type"] = "hypercube"; // not a known parameter type
    weirdType["description"] = "unknown type probe";
    props.append( weirdType );
    root["properties"] = props;
    return root;
  }
};

// A well-formed custom operator with a non-builtin shape worth pinning:
// enum + default + range + root-required list.
class WellFormedTestOperator final : public RSOperator
{
public:
  std::string name() const override { return "test:wellformed"; }
  std::string displayName() const override { return "Well Formed"; }
  std::string description() const override { return "well-formed probe"; }
  Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context )
    override
  {
    (void)params;
    (void)context;
    return Json::Value( Json::objectValue );
  }

  Json::Value schema() const override
  {
    using namespace sicnu::operators::schema;
    Json::Value props( Json::objectValue );
    props["mode"] = makeEnumParam( "mode", "pick one", { "a", "b" }, "a" );
    Json::Value level = makeNumberParam( "level", "strength", 0.5 );
    setRange( level, 0.0, 1.0 );
    props["level"] = level;
    props["note"] = makeStringParam( "note", "free text", "" );
    Json::Value root = makeRootSchema( "WellFormed", "probe", props, Json::Value( Json::objectValue ) );
    root["required"] = makeRequired( { "mode" } );
    return root;
  }
};

struct RegisteredOperators
{
  RegisteredOperators()
  {
    sicnu::operators::rs::initBuiltinRsOperators();
    RSOperatorRegistry::instance().registerOperator(
      "test:drifty", [] { return std::make_unique<DriftyOperator>(); } );
    RSOperatorRegistry::instance().registerOperator(
      "test:wellformed", [] { return std::make_unique<WellFormedTestOperator>(); } );
  }
};

const std::vector<std::string> kExpectedLiveOperators = {
  "rs:spectral_index", "rs:radiometric_calibration", "rs:band_math",
  "rs:sar_calibrate",  "rs:change_detection",        "rs:qa_mask",
};

int countDrift( const RegistryOperatorKnowledge &knowledge, const std::string &code )
{
  int n = 0;
  for ( const auto &record : knowledge.driftLog() )
    if ( record.code == code )
      ++n;
  return n;
}

} // namespace

TEST_CASE( "live registry knowledge resolves builtin rs operators", "[explain][adapter][registry]" )
{
  RegisteredOperators guard;
  RegistryOperatorKnowledge knowledge( RSOperatorRegistry::instance() );

  int resolved = 0;
  for ( const std::string &operatorId : kExpectedLiveOperators )
  {
    const std::optional<OperatorFacts> facts = knowledge.findOperator( operatorId );
    if ( !facts.has_value() )
      continue;
    ++resolved;
    CHECK( facts->id == operatorId );
    CHECK( !facts->displayName.empty() );
    CHECK( !facts->description.empty() );
  }
  // The live registry is the authority: at least five of the pinned family
  // representatives must resolve. Fewer means the builtin registration or
  // the adapter broke — the coverage oracle for authored guidance would be
  // checking against an empty world.
  REQUIRE( resolved >= 5 );
}

TEST_CASE( "parameter facts come from the live schema", "[explain][adapter][registry]" )
{
  RegisteredOperators guard;
  RegistryOperatorKnowledge knowledge( RSOperatorRegistry::instance() );

  const std::optional<OperatorFacts> spectral = knowledge.findOperator( "rs:spectral_index" );
  REQUIRE( spectral.has_value() );
  const auto indexIt = std::find_if( spectral->parameters.begin(), spectral->parameters.end(),
                                     []( const ParamFact &p ) { return p.name == "index"; } );
  REQUIRE( indexIt != spectral->parameters.end() );
  CHECK( indexIt->type == "string" );
  CHECK( !indexIt->description.empty() );
  CHECK( std::find( indexIt->enumValues.begin(), indexIt->enumValues.end(), "NDVI" )
         != indexIt->enumValues.end() );
  CHECK( indexIt->defaultValue.isString() );
  CHECK( indexIt->defaultValue.asString() == "NDVI" );

  const auto inputIt = std::find_if( spectral->parameters.begin(), spectral->parameters.end(),
                                     []( const ParamFact &p ) { return p.name == "input"; } );
  REQUIRE( inputIt != spectral->parameters.end() );
  CHECK( inputIt->required ); // raster inputs are declared required

  const auto nirIt = std::find_if( spectral->parameters.begin(), spectral->parameters.end(),
                                   []( const ParamFact &p ) { return p.name == "nir"; } );
  REQUIRE( nirIt != spectral->parameters.end() );
  CHECK( nirIt->type == "integer" );
  CHECK( nirIt->defaultValue.isIntegral() );
  CHECK( nirIt->defaultValue.asInt() == 4 );
}

TEST_CASE( "ranges and root-required lists project onto parameter facts",
           "[explain][adapter][registry]" )
{
  RegisteredOperators guard;
  RegistryOperatorKnowledge knowledge( RSOperatorRegistry::instance() );

  const std::optional<OperatorFacts> facts = knowledge.findOperator( "test:wellformed" );
  REQUIRE( facts.has_value() );
  REQUIRE( facts->parameters.size() == 3 );

  const auto modeIt = std::find_if( facts->parameters.begin(), facts->parameters.end(),
                                    []( const ParamFact &p ) { return p.name == "mode"; } );
  REQUIRE( modeIt != facts->parameters.end() );
  CHECK( modeIt->required ); // from the root "required" list
  CHECK( modeIt->enumValues.size() == 2 );

  const auto levelIt = std::find_if( facts->parameters.begin(), facts->parameters.end(),
                                     []( const ParamFact &p ) { return p.name == "level"; } );
  REQUIRE( levelIt != facts->parameters.end() );
  REQUIRE( levelIt->minimum.has_value() );
  REQUIRE( levelIt->maximum.has_value() );
  CHECK( *levelIt->minimum == 0.0 );
  CHECK( *levelIt->maximum == 1.0 );
  CHECK( !levelIt->required );

  const auto noteIt = std::find_if( facts->parameters.begin(), facts->parameters.end(),
                                    []( const ParamFact &p ) { return p.name == "note"; } );
  REQUIRE( noteIt != facts->parameters.end() );
  CHECK( !noteIt->required );
}

TEST_CASE( "unknown operators fail closed", "[explain][adapter][registry][hallucination]" )
{
  RegisteredOperators guard;
  RegistryOperatorKnowledge knowledge( RSOperatorRegistry::instance() );

  CHECK( !knowledge.findOperator( "rs:definitely_not_registered" ).has_value() );
  CHECK( !knowledge.findOperator( "" ).has_value() );
}

TEST_CASE( "schema drift is surfaced as typed records, not silently normalized",
           "[explain][adapter][registry][drift]" )
{
  RegisteredOperators guard;
  RegistryOperatorKnowledge knowledge( RSOperatorRegistry::instance() );

  // The real builtins are well-formed: resolving them must record no drift.
  REQUIRE( knowledge.findOperator( "rs:spectral_index" ).has_value() );
  CHECK( knowledge.driftLog().empty() );

  // The hostile operator must resolve (availability is fact) but its broken
  // surface must produce typed drift records and honest degradation.
  const std::optional<OperatorFacts> facts = knowledge.findOperator( "test:drifty" );
  REQUIRE( facts.has_value() );
  CHECK( facts->parameters.empty() ); // array-shaped properties: nothing projected

  // properties-not-object drift and the per-parameter records.
  CHECK( countDrift( knowledge, "schema_properties_not_object" ) == 1 );

  // The log is bounded: resolving the drifty operator many times must not
  // grow it without limit.
  for ( int i = 0; i < 5000; ++i )
    (void)knowledge.findOperator( "test:drifty" );
  CHECK( knowledge.driftLog().size() <= RegistryOperatorKnowledge::kMaxDriftRecords );
}
