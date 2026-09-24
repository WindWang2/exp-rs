// tests/test_explain_builder.cpp
//
// RS14-15 Slice D coverage: the explanation builder composes StepExplanation
// from fact sources over fakes. Pinned semantics:
//   - fail-closed on malformed requests and unknown operators;
//   - provenance classes are stamped by the builder: machine facts cite
//     machine-kind evidence, authored narrative never poses as a fact;
//   - authored guidance contradicting port facts surfaces as a typed
//     problem (state_contradiction) and never silently merges;
//   - authored rationales referencing nonexistent parameters are refused;
//   - execution evidence without machine-kind links is refused;
//   - building the same request twice yields byte-identical output.
#include <catch2/catch_test_macros.hpp>

#include "explain/explanation_builder.h"
#include "explain/state_vocabulary.h"
#include "explain/step_explanation.h"

#include <json/json.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace sicnu::explain;

namespace
{

class FakeKnowledge final : public IOperatorKnowledge
{
public:
  std::map<std::string, OperatorFacts> operators;

  std::optional<OperatorFacts> findOperator( const std::string &operatorId ) const override
  {
    const auto it = operators.find( operatorId );
    if ( it == operators.end() )
      return std::nullopt;
    return it->second;
  }
};

class FakeGuidance final : public IAuthoredGuidance
{
public:
  std::map<std::string, StepGuidance> entries;

  std::optional<StepGuidance> guidanceFor( const std::string &operatorId,
                                           const std::string &role ) const override
  {
    const auto it = entries.find( role.empty() ? operatorId : operatorId + "[" + role + "]" );
    if ( it == entries.end() )
      return std::nullopt;
    return it->second;
  }
};

class FakeEvidence final : public IExecutionEvidence
{
public:
  std::map<std::string, StepEvidence> evidence;

  std::optional<StepEvidence> evidenceFor( const std::string &runId,
                                           const std::string &stepId ) const override
  {
    const auto it = evidence.find( runId + "#" + stepId );
    if ( it == evidence.end() )
      return std::nullopt;
    return it->second;
  }
};

EvidenceLink machineLink( const std::string &target )
{
  EvidenceLink link;
  link.kind = EvidenceProvenance;
  link.target = target;
  return link;
}

ParamFact makeParam( const std::string &name, const std::string &type )
{
  ParamFact param;
  param.name = name;
  param.type = type;
  param.description = "param " + name;
  return param;
}

OperatorFacts makeOperator( const std::string &id )
{
  OperatorFacts facts;
  facts.id = id;
  facts.displayName = "Op " + id;
  facts.group = "spectral";
  facts.description = "test operator " + id;
  facts.purpose = "computes a test quantity";
  facts.parameters = { makeParam( "input", "raster" ), makeParam( "threshold", "number" ) };
  return facts;
}

struct Fixture
{
  FakeKnowledge knowledge;
  FakeGuidance guidance;
  FakeEvidence evidence;

  Fixture()
  {
    knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
    StepGuidance authored;
    authored.operatorId = "rs:test";
    authored.purpose = "teaching narrative about the test operator";
    authored.assumptions = { "surface is Lambertian" };
    AuthoredParameterRationale rationale;
    rationale.parameter = "threshold";
    rationale.rationale = "0.3 balances omission and commission";
    authored.parameterRationale = { rationale };
    AuthoredStateNarrative narrative;
    narrative.before = "DN";
    narrative.after = "TOA";
    authored.stateNarrative = narrative;
    AuthoredSkipConsequence consequence;
    consequence.summary = "downstream index layer is missing";
    consequence.downstreamRoles = { "classification" };
    authored.skipConsequence = consequence;
    guidance.entries.emplace( "rs:test", authored );
  }

  StepExplanationBuilder builder() { return StepExplanationBuilder( knowledge, guidance, &evidence ); }
};

ExplanationRequest baseRequest()
{
  ExplanationRequest request = makeOperatorRequest( WorkflowKindAgentPlan, "wf-1", "step-1",
                                                    "Test Step", "rs:test" );
  Json::Value parameters( Json::objectValue );
  parameters["threshold"] = 0.3;
  request.parameters = parameters;
  return request;
}

PortFactView makePort( const std::string &name, const std::string &state )
{
  PortFactView port;
  port.portName = name;
  port.dataType = PortDataTypeRaster;
  port.stateToken = state;
  port.crs = "EPSG:32649";
  port.resolutionX = 30.0;
  port.resolutionY = 30.0;
  port.bandCount = 1;
  return port;
}

bool hasProblem( const BuildOutcome &outcome, const std::string &code )
{
  for ( const BuildProblem &problem : outcome.problems )
    if ( problem.code == code )
      return true;
  return false;
}

} // namespace

TEST_CASE( "malformed requests fail closed", "[explain][builder]" )
{
  Fixture fixture;
  const StepExplanationBuilder builder( fixture.knowledge, fixture.guidance );

  ExplanationRequest request = baseRequest();
  request.workflowKind = "not_a_workflow_kind";
  const BuildOutcome outcome = builder.build( request );
  REQUIRE( outcome.failed() );
  CHECK( outcome.failureCode == "invalid_request" );
}

TEST_CASE( "unknown operators fail closed", "[explain][builder][hallucination]" )
{
  Fixture fixture;
  const StepExplanationBuilder builder( fixture.knowledge, fixture.guidance );

  ExplanationRequest request = baseRequest();
  request.operatorId = "rs:definitely_not_registered";
  const BuildOutcome outcome = builder.build( request );
  REQUIRE( outcome.failed() );
  CHECK( outcome.failureCode == "operator_unknown" );
}

TEST_CASE( "builder composes machine facts and authored narrative with distinct provenance",
           "[explain][builder]" )
{
  Fixture fixture;
  const BuildOutcome outcome = fixture.builder().build( baseRequest() );
  REQUIRE( !outcome.failed() );

  REQUIRE( outcome.explanation.purpose.size() == 2 );
  CHECK( outcome.explanation.purpose[0].provenance == FactProvenance::SystemFact );
  CHECK( outcome.explanation.purpose[0].text == "computes a test quantity" );
  REQUIRE( outcome.explanation.purpose[0].evidence.size() == 1 );
  CHECK( isMachineEvidenceKind( outcome.explanation.purpose[0].evidence.front().kind ) );
  CHECK( outcome.explanation.purpose[1].provenance == FactProvenance::AuthoredGuidance );
  CHECK( outcome.explanation.purpose[1].text == "teaching narrative about the test operator" );
  CHECK( !isMachineEvidenceKind( outcome.explanation.purpose[1].evidence.front().kind ) );

  REQUIRE( outcome.explanation.scientificAssumptions.size() == 1 );
  CHECK( outcome.explanation.scientificAssumptions.front().provenance
         == FactProvenance::AuthoredGuidance );

  REQUIRE( outcome.explanation.parameterRationale.size() == 1 );
  CHECK( outcome.explanation.parameterRationale.front().parameter == "threshold" );
  CHECK( outcome.explanation.parameterRationale.front().chosenValue.asDouble() == 0.3 );
  CHECK( outcome.explanation.parameterRationale.front().provenance
         == FactProvenance::AuthoredGuidance );
}

TEST_CASE( "authored rationale referencing a nonexistent parameter is a typed problem",
           "[explain][builder][hallucination]" )
{
  Fixture fixture;
  fixture.guidance.entries["rs:test"].parameterRationale.front().parameter = "ghost_param";

  const BuildOutcome outcome = fixture.builder().build( baseRequest() );
  CHECK( !outcome.failed() );
  CHECK( hasProblem( outcome, "unknown_parameter_reference" ) );
  CHECK( outcome.explanation.parameterRationale.empty() );
}

TEST_CASE( "port facts synthesize state transitions with mixed divergence",
           "[explain][builder]" )
{
  Fixture fixture;
  ExplanationRequest request = baseRequest();
  request.inputPorts = { makePort( "in", "DN" ), makePort( "in2", "DN" ) };
  request.outputPorts = { makePort( "out", "TOA" ) };

  const BuildOutcome outcome = fixture.builder().build( request );
  REQUIRE( !outcome.failed() );

  bool foundRadiometric = false;
  for ( const StateTransition &transition : outcome.explanation.stateChanges )
  {
    if ( transition.aspect != AspectRadiometricState )
      continue;
    foundRadiometric = true;
    CHECK( transition.provenance == FactProvenance::InferredExplanation );
    CHECK( isMachineEvidenceKind( transition.evidence.front().kind ) );
    CHECK( transition.before == "DN" );
    CHECK( transition.after == "TOA" );
    // The authored narrative agrees (DN -> TOA): no contradiction problem.
    CHECK( !hasProblem( outcome, "state_contradiction" ) );
  }
  CHECK( foundRadiometric );
}

TEST_CASE( "divergent port declarations surface as mixed and are stated honestly",
           "[explain][builder]" )
{
  Fixture fixture;
  ExplanationRequest request = baseRequest();
  request.inputPorts = { makePort( "in", "DN" ), makePort( "in2", "TOA" ) };
  request.outputPorts = { makePort( "out", "TOA" ) };

  const BuildOutcome outcome = fixture.builder().build( request );
  REQUIRE( !outcome.failed() );
  // "mixed" is the honest sentinel: the authored narrative (DN -> TOA) is
  // NOT comparable against a divergent declaration, so no contradiction is
  // claimed — the divergence itself is stated in the trust notes instead.
  CHECK( !hasProblem( outcome, "state_contradiction" ) );
  bool sawMixed = false;
  bool divergenceNoted = false;
  for ( const StateTransition &transition : outcome.explanation.stateChanges )
  {
    if ( transition.aspect == AspectRadiometricState )
    {
      CHECK( transition.before == "mixed" );
      sawMixed = true;
    }
  }
  for ( const std::string &note : outcome.explanation.trustNotes )
    if ( note.find( "mixed" ) != std::string::npos )
      divergenceNoted = true;
  CHECK( sawMixed );
  CHECK( divergenceNoted );
}

TEST_CASE( "authored narrative fills gaps only where ports declare nothing",
           "[explain][builder]" )
{
  Fixture fixture;
  ExplanationRequest request = baseRequest(); // no ports at all

  const BuildOutcome outcome = fixture.builder().build( request );
  REQUIRE( !outcome.failed() );

  bool authoredOnly = false;
  for ( const StateTransition &transition : outcome.explanation.stateChanges )
  {
    if ( transition.aspect == AspectRadiometricState )
    {
      authoredOnly = true;
      CHECK( transition.provenance == FactProvenance::AuthoredGuidance );
      CHECK( transition.before == "DN" );
      CHECK( transition.after == "TOA" );
    }
  }
  CHECK( authoredOnly );
}

TEST_CASE( "execution evidence without machine-kind links is refused",
           "[explain][builder][hallucination]" )
{
  Fixture fixture;
  ExplanationRequest request = baseRequest();
  request.runId = "run-9";

  StepEvidence ungrounded;
  ungrounded.status = "Succeeded";
  EvidenceLink authoredLink;
  authoredLink.kind = EvidenceGuidance;
  authoredLink.target = "guidance:rs:test";
  ungrounded.links = { authoredLink };
  fixture.evidence.evidence.emplace( "run-9#step-1", ungrounded );

  const BuildOutcome outcome = fixture.builder().build( request );
  CHECK( !outcome.failed() );
  CHECK( hasProblem( outcome, "ungrounded_execution_evidence" ) );
  CHECK( !outcome.explanation.execution.has_value() );

  // The same evidence with a machine-kind link is attached.
  fixture.evidence.evidence["run-9#step-1"].links.push_back( machineLink( "provenance:run-9#node:step-1" ) );
  const BuildOutcome grounded = fixture.builder().build( request );
  REQUIRE( grounded.explanation.execution.has_value() );
  CHECK( grounded.explanation.execution->status == "Succeeded" );
  CHECK( !hasProblem( grounded, "ungrounded_execution_evidence" ) );
}

TEST_CASE( "skipped steps surface their runtime state honestly", "[explain][builder]" )
{
  Fixture fixture;
  ExplanationRequest request = baseRequest();
  request.executionStatus = "Skipped";

  const BuildOutcome outcome = fixture.builder().build( request );
  REQUIRE( !outcome.failed() );
  REQUIRE( outcome.explanation.skipConsequence.has_value() );
  CHECK( outcome.explanation.skipConsequence->provenance == FactProvenance::AuthoredGuidance );

  bool noted = false;
  for ( const std::string &note : outcome.explanation.trustNotes )
    if ( note.find( "Skipped" ) != std::string::npos )
      noted = true;
  CHECK( noted );
}

TEST_CASE( "non-operator steps produce a skeleton without operator facts",
           "[explain][builder]" )
  {
  Fixture fixture;
  const StepExplanationBuilder builder( fixture.knowledge, fixture.guidance );

  ExplanationRequest request;
  request.workflowKind = WorkflowKindD17Designer;
  request.workflowId = "wf-2";
  request.stepId = "review-1";
  request.stepTitle = "Peer review";
  request.stepKind = StepKindReview;

  const BuildOutcome outcome = builder.build( request );
  REQUIRE( !outcome.failed() );
  CHECK( outcome.explanation.operatorId.empty() );
  CHECK( outcome.explanation.purpose.empty() );
  CHECK( !outcome.explanation.trustNotes.empty() );
}

TEST_CASE( "building the same request twice is byte-identical", "[explain][builder][determinism]" )
{
  Fixture fixture;
  const BuildOutcome first = fixture.builder().build( baseRequest() );
  const BuildOutcome second = fixture.builder().build( baseRequest() );

  const std::string canonicalFirst = stepExplanationToCanonicalJson( first.explanation );
  const std::string canonicalSecond = stepExplanationToCanonicalJson( second.explanation );
  CHECK( canonicalFirst == canonicalSecond );
  CHECK( !canonicalFirst.empty() );
}
