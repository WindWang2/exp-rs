#include "explain/explanation_validator.h"

#include "explain/state_vocabulary.h"

#include <algorithm>

namespace sicnu::explain
{
namespace
{

void addIssue( std::vector<ValidationIssue> &issues, const std::string &code,
               const std::string &field, const std::string &message )
{
  issues.push_back( { code, field, message } );
}

bool hasMachineEvidence( const std::vector<EvidenceLink> &links )
{
  return std::any_of( links.begin(), links.end(),
                      []( const EvidenceLink &l ) { return isMachineEvidenceKind( l.kind ); } );
}

void checkGroundedTexts( const std::vector<GroundedText> &entries, const std::string &field,
                         std::vector<ValidationIssue> &issues )
{
  for ( size_t i = 0; i < entries.size(); ++i )
  {
    const GroundedText &entry = entries[i];
    const std::string context = field + "[" + std::to_string( i ) + "]";
    if ( entry.text.empty() )
      addIssue( issues, "empty_content", context, "entry text must not be empty" );
    if ( entry.provenance == FactProvenance::SystemFact && !hasMachineEvidence( entry.evidence ) )
      addIssue( issues, "system_fact_without_machine_evidence", context,
                "system fact requires machine-kind evidence" );
    if ( entry.provenance == FactProvenance::InferredExplanation && !hasMachineEvidence( entry.evidence ) )
      addIssue( issues, "inferred_without_evidence", context,
                "inferred narrative requires machine-kind evidence for its grounding" );
  }
}

} // namespace

ExplanationValidator::ExplanationValidator( const IOperatorKnowledge &operators,
                                            const IAuthoredGuidance *guidance )
  : operators_( operators )
  , guidance_( guidance )
{
}

ValidationReport ExplanationValidator::validate( const StepExplanation &explanation ) const
{
  ValidationReport report;
  std::vector<ValidationIssue> &issues = report.issues;

  if ( explanation.schemaVersion != StepExplanationSchemaV1 )
    addIssue( issues, "schema_version_unsupported", "schemaVersion",
              "unsupported schema version '" + explanation.schemaVersion + "'" );
  if ( !isKnownWorkflowKind( explanation.workflowKind ) )
    addIssue( issues, "unknown_workflow_kind", "workflowKind",
              "unknown workflow kind '" + explanation.workflowKind + "'" );
  if ( !isKnownStepKind( explanation.stepKind ) )
    addIssue( issues, "unknown_step_kind", "stepKind",
              "unknown step kind '" + explanation.stepKind + "'" );
  if ( explanation.workflowId.empty() || explanation.stepId.empty() )
    addIssue( issues, "invalid_identity", "workflowId/stepId", "workflow and step ids are required" );

  std::optional<OperatorFacts> facts;
  if ( explanation.stepKind == StepKindOperator )
  {
    if ( explanation.operatorId.empty() )
      addIssue( issues, "missing_operator", "operatorId", "operator steps require an operatorId" );
    else
    {
      facts = operators_.findOperator( explanation.operatorId );
      if ( !facts.has_value() )
        addIssue( issues, "unknown_operator", "operatorId",
                  "operator '" + explanation.operatorId + "' is not registered" );
    }
  }

  checkGroundedTexts( explanation.purpose, "purpose", issues );
  checkGroundedTexts( explanation.prerequisites, "prerequisites", issues );
  checkGroundedTexts( explanation.scientificAssumptions, "scientificAssumptions", issues );

  for ( size_t i = 0; i < explanation.stateChanges.size(); ++i )
  {
    const StateTransition &transition = explanation.stateChanges[i];
    const std::string context = "stateChanges[" + std::to_string( i ) + "]";
    if ( !isKnownStateAspect( transition.aspect ) )
      addIssue( issues, "unknown_state_aspect", context,
                "unknown aspect '" + transition.aspect + "'" );
    if ( transition.aspect == AspectRadiometricState )
    {
      // radiometric tokens must be vocabulary members, the "mixed" sentinel
      // or empty (undeclared); crs/resolution/band_count carry free-form text
      for ( const std::string *token : { &transition.before, &transition.after } )
      {
        if ( token->empty() || *token == "mixed" )
          continue;
        if ( !isKnownStateToken( *token ) )
          addIssue( issues, "unknown_state_token", context,
                    "unknown state token '" + *token + "'" );
      }
    }
    if ( transition.explanation.empty() )
      addIssue( issues, "empty_content", context, "explanation text is required" );
    if ( transition.provenance == FactProvenance::SystemFact && !hasMachineEvidence( transition.evidence ) )
      addIssue( issues, "system_fact_without_machine_evidence", context,
                "system fact requires machine-kind evidence" );
    if ( transition.provenance == FactProvenance::InferredExplanation &&
         !hasMachineEvidence( transition.evidence ) )
      addIssue( issues, "inferred_without_evidence", context,
                "inferred transition requires machine-kind evidence" );
  }

  for ( size_t i = 0; i < explanation.parameterRationale.size(); ++i )
  {
    const ParameterRationale &rationale = explanation.parameterRationale[i];
    const std::string context = "parameterRationale[" + std::to_string( i ) + "]";
    if ( rationale.parameter.empty() || rationale.rationale.empty() )
      addIssue( issues, "empty_content", context, "parameter and rationale are required" );
    if ( facts.has_value() )
    {
      const bool known = std::any_of( facts->parameters.begin(), facts->parameters.end(),
                                      [ &rationale ]( const ParamFact &p )
                                      { return p.name == rationale.parameter; } );
      if ( !known )
        addIssue( issues, "unknown_parameter_reference", context,
                  "parameter '" + rationale.parameter + "' does not exist in the schema of " +
                    facts->id );
    }
    if ( rationale.provenance == FactProvenance::SystemFact && !hasMachineEvidence( rationale.evidence ) )
      addIssue( issues, "system_fact_without_machine_evidence", context,
                "system fact requires machine-kind evidence" );
    if ( rationale.provenance == FactProvenance::InferredExplanation &&
         !hasMachineEvidence( rationale.evidence ) )
      addIssue( issues, "inferred_without_evidence", context,
                "inferred rationale requires machine-kind evidence" );
  }

  if ( explanation.skipConsequence.has_value() )
  {
    if ( explanation.skipConsequence->summary.empty() )
      addIssue( issues, "empty_content", "skipConsequence.summary", "summary is required" );
    for ( const std::string &role : explanation.skipConsequence->downstreamRoles )
    {
      if ( role.empty() )
        addIssue( issues, "empty_content", "skipConsequence.downstreamRoles",
                  "roles must be non-empty" );
    }
  }

  if ( explanation.execution.has_value() && !hasMachineEvidence( explanation.execution->evidence ) )
    addIssue( issues, "system_fact_without_machine_evidence", "execution",
              "execution facts require machine-kind evidence" );

  for ( size_t i = 0; i < explanation.evidenceLinks.size(); ++i )
  {
    if ( !explanation.evidenceLinks[i].isValid() )
      addIssue( issues, "invalid_evidence_link",
                "evidenceLinks[" + std::to_string( i ) + "]", "malformed link" );
  }

  for ( const SourceReference &reference : explanation.sourceReferences )
  {
    if ( !isKnownReferenceKind( reference.kind ) )
      addIssue( issues, "invalid_reference_kind", "sourceReferences",
                "unknown reference kind '" + reference.kind + "'" );
    if ( reference.title.empty() || reference.locator.empty() )
      addIssue( issues, "empty_content", "sourceReferences", "title and locator are required" );
  }

  for ( const std::string &note : explanation.trustNotes )
  {
    if ( note.empty() )
      addIssue( issues, "empty_content", "trustNotes", "notes must be non-empty" );
  }

  return report;
}

std::vector<ValidationIssue> ExplanationValidator::validateGuidance(
  const StepGuidance &guidance ) const
{
  std::vector<ValidationIssue> issues;

  if ( guidance.operatorId.empty() )
    addIssue( issues, "missing_operator", "operatorId", "guidance must name an operator" );
  else if ( !operators_.findOperator( guidance.operatorId ).has_value() )
    addIssue( issues, "unknown_operator", "operatorId",
              "operator '" + guidance.operatorId + "' is not registered" );

  if ( guidance.purpose.empty() )
    addIssue( issues, "empty_content", "purpose", "purpose is required" );

  if ( !operators_.findOperator( guidance.operatorId ).has_value() )
    return issues; // deeper checks need the live schema

  const std::optional<OperatorFacts> facts = operators_.findOperator( guidance.operatorId );
  for ( const AuthoredParameterRationale &rationale : guidance.parameterRationale )
  {
    const bool known = facts.has_value() &&
                       std::any_of( facts->parameters.begin(), facts->parameters.end(),
                                    [ &rationale ]( const ParamFact &p )
                                    { return p.name == rationale.parameter; } );
    if ( !known )
      addIssue( issues, "unknown_parameter_reference", "parameterRationale",
                "parameter '" + rationale.parameter + "' does not exist in the schema of " +
                  guidance.operatorId );
  }
  if ( guidance.stateNarrative.has_value() )
  {
    for ( const std::string *token :
          { &guidance.stateNarrative->before, &guidance.stateNarrative->after } )
    {
      if ( !token->empty() && !isKnownStateToken( *token ) )
        addIssue( issues, "unknown_state_token", "stateNarrative",
                  "unknown state token '" + *token + "'" );
    }
  }
  for ( const AuthoredReference &reference : guidance.references )
  {
    if ( !isKnownReferenceKind( reference.kind ) )
      addIssue( issues, "invalid_reference_kind", "references",
                "unknown reference kind '" + reference.kind + "'" );
  }
  return issues;
}

} // namespace sicnu::explain
