/***************************************************************************
 * explanation_validator.h — hallucination guard for step explanations
 *
 * Independent of the builder: validates any StepExplanation (built or
 * parsed) against the live fact sources. A hand-forged or AI-generated
 * explanation cannot pass with references to parameters that do not exist,
 * state tokens outside the vocabulary, or SystemFact/inferred content
 * without machine-kind evidence.
 ***************************************************************************/
#pragma once

#include "explain/authored_guidance.h"
#include "explain/explanation_sources.h"
#include "explain/step_explanation.h"

#include <string>
#include <vector>

namespace sicnu::explain
{

struct ValidationIssue
{
  std::string code; // typed, machine-readable
  std::string field;
  std::string message;
};

struct ValidationReport
{
  std::vector<ValidationIssue> issues;
  bool ok() const { return issues.empty(); }
};

class ExplanationValidator
{
public:
  explicit ExplanationValidator( const IOperatorKnowledge &operators,
                                 const IAuthoredGuidance *guidance = nullptr );

  // Cross-checks a complete explanation. Operator steps are resolved against
  // the live operator knowledge; authored guidance (when provided) is used to
  // verify source references.
  ValidationReport validate( const StepExplanation &explanation ) const;

  // Semantic validation of one authored guidance entry against the live
  // operator knowledge (the structural validation happens in the store).
  std::vector<ValidationIssue> validateGuidance( const StepGuidance &guidance ) const;

private:
  const IOperatorKnowledge &operators_;
  const IAuthoredGuidance *guidance_;
};

} // namespace sicnu::explain
