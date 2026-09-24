// tests/test_explain_validator.cpp
//
// RS14-15 Slice G coverage: the hallucination-guard validator. A hand-forged
// or AI-generated explanation must fail validation whenever it references
// operators/parameters that do not exist, uses state tokens outside the
// vocabulary, or claims system-fact/inferred content without machine-kind
// evidence. Adversarial rule: any test below kills the corresponding
// "validator accepts forged content" mutation.
#include <catch2/catch_test_macros.hpp>

#include "explain/explanation_validator.h"

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

EvidenceLink machineLink()
{
  EvidenceLink link;
  link.kind = EvidenceDerivation;
  link.target = "derivation:asset-1#step-1";
  return link;
}

EvidenceLink authoredLink()
{
  EvidenceLink link;
  link.kind = EvidenceGuidance;
  link.target = "guidance:rs:test";
  return link;
}

ParamFact makeParam( const std::string &name )
{
  ParamFact param;
  param.name = name;
  param.type = "number";
  return param;
}

OperatorFacts makeOperator( const std::string &id )
{
  OperatorFacts facts;
  facts.id = id;
  facts.displayName = "Op " + id;
  facts.parameters = { makeParam( "threshold" ), makeParam( "input" ) };
  return facts;
}

StepExplanation baseExplanation()
{
  StepExplanation explanation;
  explanation.workflowKind = WorkflowKindAgentPlan;
  explanation.workflowId = "wf-1";
  explanation.stepId = "step-1";
  explanation.stepKind = StepKindOperator;
  explanation.operatorId = "rs:test";
  return explanation;
}

struct IssueCodes
{
  explicit IssueCodes( const ValidationReport &report )
  {
    for ( const ValidationIssue &issue : report.issues )
      codes.push_back( issue.code );
  }
  explicit IssueCodes( const std::vector<ValidationIssue> &issues )
  {
    for ( const ValidationIssue &issue : issues )
      codes.push_back( issue.code );
  }
  bool has( const std::string &code ) const
  {
    for ( const std::string &c : codes )
      if ( c == code )
        return true;
    return false;
  }
  std::vector<std::string> codes;
};

} // namespace

TEST_CASE( "a builder-shaped explanation passes validation", "[explain][validator]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepExplanation explanation = baseExplanation();
  GroundedText purpose;
  purpose.text = "machine purpose";
  purpose.provenance = FactProvenance::SystemFact;
  purpose.evidence = { machineLink() };
  explanation.purpose = { purpose };

  const ValidationReport report = validator.validate( explanation );
  CHECK( report.ok() );
}

TEST_CASE( "identity and vocabulary violations are typed", "[explain][validator]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepExplanation explanation = baseExplanation();
  explanation.schemaVersion = "exp.step_explanation.v0";
  explanation.workflowKind = "made_up_kind";
  explanation.stepKind = "made_up_step";
  explanation.workflowId.clear();

  const IssueCodes codes( validator.validate( explanation ) );
  CHECK( codes.has( "schema_version_unsupported" ) );
  CHECK( codes.has( "unknown_workflow_kind" ) );
  CHECK( codes.has( "unknown_step_kind" ) );
  CHECK( codes.has( "invalid_identity" ) );
  // Operator resolution runs only for actual operator steps; an unknown
  // step kind is already flagged above.
  StepExplanation ghost = baseExplanation();
  ghost.operatorId = "rs:ghost";
  const IssueCodes ghostCodes( validator.validate( ghost ) );
  CHECK( ghostCodes.has( "unknown_operator" ) );
}

TEST_CASE( "operator steps without an operatorId are refused", "[explain][validator]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepExplanation explanation = baseExplanation();
  explanation.operatorId.clear();
  const IssueCodes codes( validator.validate( explanation ) );
  CHECK( codes.has( "missing_operator" ) );
}

TEST_CASE( "system-fact content without machine evidence cannot pass",
           "[explain][validator][hallucination]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepExplanation explanation = baseExplanation();
  GroundedText forged;
  forged.text = "claims a runtime fact";
  forged.provenance = FactProvenance::SystemFact;
  forged.evidence = { authoredLink() }; // guidance is authored, never machine evidence
  explanation.purpose = { forged };

  const IssueCodes codes( validator.validate( explanation ) );
  CHECK( codes.has( "system_fact_without_machine_evidence" ) );
}

TEST_CASE( "inferred narrative without grounding evidence cannot pass",
           "[explain][validator][hallucination]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepExplanation explanation = baseExplanation();
  StateTransition transition;
  transition.aspect = AspectRadiometricState;
  transition.before = "DN";
  transition.after = "TOA";
  transition.explanation = "the step converts DN to TOA";
  transition.provenance = FactProvenance::InferredExplanation;
  transition.evidence = {}; // no grounding at all
  explanation.stateChanges = { transition };

  const IssueCodes codes( validator.validate( explanation ) );
  CHECK( codes.has( "inferred_without_evidence" ) );
}

TEST_CASE( "state vocabulary and aspects are closed", "[explain][validator]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepExplanation explanation = baseExplanation();
  StateTransition unknownAspect;
  unknownAspect.aspect = "albedo_profile"; // not in the aspect vocabulary
  unknownAspect.before = "DN";
  unknownAspect.after = "TOA";
  unknownAspect.explanation = "made up";
  unknownAspect.provenance = FactProvenance::InferredExplanation;
  unknownAspect.evidence = { machineLink() };
  StateTransition unknownToken;
  unknownToken.aspect = AspectRadiometricState;
  unknownToken.before = "DN";
  unknownToken.after = "NIRish"; // not in the token vocabulary
  unknownToken.explanation = "made up";
  unknownToken.provenance = FactProvenance::InferredExplanation;
  unknownToken.evidence = { machineLink() };
  explanation.stateChanges = { unknownAspect, unknownToken };

  const IssueCodes codes( validator.validate( explanation ) );
  CHECK( codes.has( "unknown_state_aspect" ) );
  CHECK( codes.has( "unknown_state_token" ) );
}

TEST_CASE( "parameter rationales are checked against the live operator schema",
           "[explain][validator][hallucination]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepExplanation explanation = baseExplanation();
  ParameterRationale good;
  good.parameter = "threshold";
  good.rationale = "why 0.3";
  ParameterRationale forged;
  forged.parameter = "ghost";
  forged.rationale = "references a parameter that does not exist";
  explanation.parameterRationale = { good, forged };

  const IssueCodes codes( validator.validate( explanation ) );
  CHECK( codes.has( "unknown_parameter_reference" ) );
}

TEST_CASE( "execution facts without machine evidence are refused",
           "[explain][validator][hallucination]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepExplanation explanation = baseExplanation();
  ExecutionFacts execution;
  execution.status = "Succeeded";
  execution.evidence = { authoredLink() };
  explanation.execution = execution;

  const IssueCodes codes( validator.validate( explanation ) );
  CHECK( codes.has( "system_fact_without_machine_evidence" ) );
}

TEST_CASE( "malformed evidence links and citations are typed", "[explain][validator]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepExplanation explanation = baseExplanation();
  EvidenceLink malformed;
  malformed.kind = "twitter";
  malformed.target = "https://example.com";
  explanation.evidenceLinks = { malformed };

  SourceReference reference;
  reference.title = "A source";
  reference.kind = "blog";
  reference.locator = "p. 3";
  explanation.sourceReferences = { reference };

  const IssueCodes codes( validator.validate( explanation ) );
  CHECK( codes.has( "invalid_evidence_link" ) );
  CHECK( codes.has( "invalid_reference_kind" ) );
}

TEST_CASE( "guidance entries are validated against the live schema",
           "[explain][validator]" )
{
  FakeKnowledge knowledge;
  knowledge.operators.emplace( "rs:test", makeOperator( "rs:test" ) );
  const ExplanationValidator validator( knowledge );

  StepGuidance good;
  good.operatorId = "rs:test";
  good.purpose = "teach the operator";
  AuthoredParameterRationale rationale;
  rationale.parameter = "input";
  rationale.rationale = "the required input";
  good.parameterRationale = { rationale };
  const ValidationReport goodReport;
  CHECK( validator.validateGuidance( good ).empty() );

  StepGuidance ghost = good;
  ghost.parameterRationale.front().parameter = "ghost";
  ghost.stateNarrative = AuthoredStateNarrative{ "DN", "PLANKTON" }; // bad token
  const IssueCodes codes( validator.validateGuidance( ghost ) );
  CHECK( codes.has( "unknown_parameter_reference" ) );
  CHECK( codes.has( "unknown_state_token" ) );

  StepGuidance orphan;
  orphan.operatorId = "rs:ghost";
  orphan.purpose = "no such operator";
  const IssueCodes orphanCodes( validator.validateGuidance( orphan ) );
  CHECK( orphanCodes.has( "unknown_operator" ) );
}
