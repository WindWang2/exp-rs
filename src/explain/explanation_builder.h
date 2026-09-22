/***************************************************************************
 * explanation_builder.h — composes StepExplanation from fact sources
 *
 * The builder is the only component allowed to stamp provenance classes.
 * It never invents content: machine facts come from the sources, authored
 * text comes from the guidance store, and composed narratives cite the
 * facts they derive from. Unknown operators fail closed; authored content
 * contradicting observable facts is surfaced, never silently merged.
 ***************************************************************************/
#pragma once

#include "explain/authored_guidance.h"
#include "explain/explanation_request.h"
#include "explain/explanation_sources.h"
#include "explain/step_explanation.h"

#include <string>
#include <vector>

namespace sicnu::explain
{

struct BuildProblem
{
  std::string code; // typed: unknown_parameter_reference | state_contradiction | ...
  std::string field;
  std::string message;
};

struct BuildOutcome
{
  StepExplanation explanation;
  std::vector<BuildProblem> problems; // non-fatal, always visible to callers

  std::string failureCode; // non-empty => hard failure, explanation is a skeleton
  std::string failureMessage;
  bool failed() const { return !failureCode.empty(); }
};

class StepExplanationBuilder
{
public:
  // The evidence source is optional: explanations of un-executed plans are
  // valid without it.
  explicit StepExplanationBuilder( const IOperatorKnowledge &operators,
                                   const IAuthoredGuidance &guidance,
                                   const IExecutionEvidence *evidence = nullptr );

  BuildOutcome build( const ExplanationRequest &request ) const;

private:
  const IOperatorKnowledge &operators_;
  const IAuthoredGuidance &guidance_;
  const IExecutionEvidence *evidence_;
};

} // namespace sicnu::explain
