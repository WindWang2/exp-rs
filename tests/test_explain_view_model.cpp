// tests/test_explain_view_model.cpp
//
// RS14-15 Slice F coverage (model level): the presentation model projects a
// StepExplanation into ordered badge-tagged sections and a deterministic
// markdown rendering. UI code renders this model — so its determinism and
// its honest handling of unbound/empty content are pinned here.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "explain/step_explanation_view_model.h"

#include <json/json.h>

#include <string>

using namespace sicnu::explain;

namespace
{

EvidenceLink machineLink()
{
  EvidenceLink link;
  link.kind = EvidenceOperationLog;
  link.target = "operation_log:op-7";
  return link;
}

StepExplanation populatedExplanation()
{
  StepExplanation explanation;
  explanation.workflowKind = WorkflowKindLab;
  explanation.workflowId = "lab-1";
  explanation.stepId = "step-2";
  explanation.stepTitle = "Radiometric calibration";
  explanation.stepKind = StepKindOperator;
  explanation.operatorId = "rs:radiometric_calibration";
  explanation.operatorDisplayName = "Radiometric Calibration";

  GroundedText fact;
  fact.text = "DN converted to radiance";
  fact.provenance = FactProvenance::SystemFact;
  fact.evidence = { machineLink() };
  GroundedText narrative;
  narrative.text = "recall the Planck relationship";
  narrative.provenance = FactProvenance::AuthoredGuidance;
  narrative.evidence = { machineLink() };
  explanation.purpose = { fact, narrative };

  StateTransition transition;
  transition.aspect = AspectRadiometricState;
  transition.before = "DN";
  transition.after = "Radiance";
  transition.explanation = "synthesized from port declarations";
  transition.provenance = FactProvenance::InferredExplanation;
  transition.evidence = { machineLink() };
  explanation.stateChanges = { transition };

  ParameterRationale rationale;
  rationale.parameter = "unit";
  rationale.chosenValue = Json::Value( "radiance" );
  rationale.rationale = "matches the sensor specification";
  rationale.provenance = FactProvenance::AuthoredGuidance;
  rationale.evidence = { machineLink() };
  explanation.parameterRationale = { rationale };

  ExecutionFacts execution;
  execution.status = "Succeeded";
  execution.elapsedMs = 1234;
  execution.cacheHit = false;
  execution.evidence = { machineLink() };
  explanation.execution = execution;

  explanation.trustNotes = { "本算子暂无编写指引（authored guidance）" };
  return explanation;
}

const ExplanationSection *findSection( const StepExplanationViewModel &model, const std::string &id )
{
  for ( const ExplanationSection &section : model.sections )
    if ( section.id == id )
      return &section;
  return nullptr;
}

} // namespace

TEST_CASE( "sections render in canonical order with provenance badges",
           "[explain][view_model]" )
{
  const StepExplanationViewModel model = StepExplanationViewModel::fromExplanation( populatedExplanation() );

  REQUIRE( model.sections.size() == 4 );
  CHECK( model.sections[0].id == "why" );
  CHECK( model.sections[1].id == "state" );
  CHECK( model.sections[2].id == "parameters" );
  CHECK( model.sections[3].id == "execution" );

  REQUIRE( model.sections[0].lines.size() == 2 );
  CHECK( model.sections[0].lines[0].badge == "系统事实" );
  CHECK( model.sections[0].lines[1].badge == "编写指引" );
  CHECK( model.sections[0].lines[0].evidenceNote == "operation_log:op-7" );

  REQUIRE( model.sections[2].lines.size() == 1 );
  CHECK( model.sections[2].lines[0].text.find( "unit = \"radiance\"" ) != std::string::npos );

  CHECK( findSection( model, "skip" ) == nullptr ); // empty sections are omitted
  CHECK( findSection( model, "sources" ) == nullptr );
}

TEST_CASE( "headline and operator line compose deterministically", "[explain][view_model]" )
{
  const StepExplanationViewModel model = StepExplanationViewModel::fromExplanation( populatedExplanation() );
  CHECK( model.headline == "step-2 — Radiometric calibration" );
  CHECK( model.operatorLine == "rs:radiometric_calibration (Radiometric Calibration)" );

  StepExplanation minimal = populatedExplanation();
  minimal.stepTitle = "step-2";
  minimal.operatorDisplayName.clear();
  const StepExplanationViewModel minimalModel = StepExplanationViewModel::fromExplanation( minimal );
  CHECK( minimalModel.headline == "step-2" );
  CHECK( minimalModel.operatorLine == "rs:radiometric_calibration" );
}

TEST_CASE( "unbound parameter values are stated as unbound, never fabricated",
           "[explain][view_model][hallucination]" )
{
  StepExplanation explanation = populatedExplanation();
  explanation.parameterRationale.front().chosenValue = Json::Value();

  const StepExplanationViewModel model = StepExplanationViewModel::fromExplanation( explanation );
  CHECK( model.sections[2].lines[0].text.find( "（未绑定）" ) != std::string::npos );
}

TEST_CASE( "markdown rendering is byte-stable across renders", "[explain][view_model][determinism]" )
{
  const StepExplanation first = populatedExplanation();
  const StepExplanation second = populatedExplanation();

  const StepExplanationViewModel firstModel = StepExplanationViewModel::fromExplanation( first );
  const std::string markdownA = firstModel.toMarkdown();
  const std::string markdownB = firstModel.toMarkdown();
  const StepExplanationViewModel secondModel = StepExplanationViewModel::fromExplanation( second );
  const std::string markdownC = secondModel.toMarkdown();

  CHECK( markdownA == markdownB );
  CHECK( markdownA == markdownC );
  CHECK( markdownA.find( "[系统事实] DN converted to radiance" ) != std::string::npos );
  CHECK( markdownA.find( "## 注意（Trust notes）" ) != std::string::npos );
  CHECK( markdownA.find( "耗时 1234 ms" ) != std::string::npos );
}
