#include "explain/explanation_builder.h"

#include <algorithm>
#include <functional>

namespace sicnu::explain
{
namespace
{

EvidenceLink operatorSchemaLink( const std::string &operatorId )
{
  EvidenceLink link;
  link.kind = EvidenceOperatorSchema;
  link.target = "operator:" + operatorId;
  return link;
}

EvidenceLink workflowDocumentLink( const std::string &workflowId, const std::string &stepId )
{
  EvidenceLink link;
  link.kind = EvidenceWorkflowDocument;
  link.target = "workflow:" + workflowId + "#" + stepId;
  return link;
}

EvidenceLink guidanceLink( const std::string &operatorId )
{
  EvidenceLink link;
  link.kind = EvidenceGuidance;
  link.target = "guidance:" + operatorId;
  return link;
}

std::string numberToString( double value )
{
  // Stable text form for port numbers (resolutions, band counts).
  if ( value == static_cast<double>( static_cast<long long>( value ) ) )
  {
    return std::to_string( static_cast<long long>( value ) );
  }
  std::string text = std::to_string( value );
  text.erase( text.find_last_not_of( '0' ) + 1 );
  if ( !text.empty() && text.back() == '.' )
    text.pop_back();
  return text;
}

// Collect declared values of one aspect across ports; returns false when no
// port declares it. Divergent declarations yield the "mixed" sentinel.
bool collectPortValues( const std::vector<PortFactView> &ports,
                        const std::function<std::string( const PortFactView & )> &extract,
                        std::string &out )
{
  std::vector<std::string> declared;
  for ( const PortFactView &port : ports )
  {
    const std::string value = extract( port );
    if ( !value.empty() )
      declared.push_back( value );
  }
  if ( declared.empty() )
    return false;
  const bool uniform = std::all_of( declared.begin(), declared.end(),
                                    [ &declared ]( const std::string &v ) { return v == declared.front(); } );
  out = uniform ? declared.front() : "mixed";
  return true;
}

} // namespace

StepExplanationBuilder::StepExplanationBuilder( const IOperatorKnowledge &operators,
                                                const IAuthoredGuidance &guidance,
                                                const IExecutionEvidence *evidence )
  : operators_( operators )
  , guidance_( guidance )
  , evidence_( evidence )
{
}

BuildOutcome StepExplanationBuilder::build( const ExplanationRequest &request ) const
{
  BuildOutcome outcome;

  // --- request validation: malformed requests never yield an explanation
  if ( !isKnownWorkflowKind( request.workflowKind ) || !isKnownStepKind( request.stepKind ) ||
       request.workflowId.empty() || request.stepId.empty() )
  {
    outcome.failureCode = "invalid_request";
    outcome.failureMessage = "request must carry a known workflowKind/stepKind and non-empty "
                             "workflowId/stepId";
    return outcome;
  }

  StepExplanation &explanation = outcome.explanation;
  explanation.workflowKind = request.workflowKind;
  explanation.workflowId = request.workflowId;
  explanation.stepId = request.stepId;
  explanation.stepTitle = request.stepTitle;
  explanation.stepKind = request.stepKind;

  const EvidenceLink documentLink = workflowDocumentLink( request.workflowId, request.stepId );

  // --- operator facts (fail-closed on unknown operators)
  std::optional<OperatorFacts> facts;
  if ( request.isOperatorStep() )
  {
    facts = operators_.findOperator( request.operatorId );
    if ( !facts.has_value() )
    {
      outcome.failureCode = "operator_unknown";
      outcome.failureMessage = "operator '" + request.operatorId + "' is not registered";
      return outcome;
    }
    explanation.operatorId = facts->id;
    explanation.operatorDisplayName = facts->displayName;
    explanation.evidenceLinks.push_back( operatorSchemaLink( facts->id ) );
  }
  else
  {
    explanation.trustNotes.push_back(
      "非算子步骤（stepKind=" + request.stepKind + "）：无算子级科学事实，仅含执行与编排信息" );
  }
  explanation.evidenceLinks.push_back( documentLink );

  // --- authored guidance (optional per operator)
  const std::optional<StepGuidance> guidance =
    guidance_.guidanceFor( request.operatorId, std::string() );
  if ( request.isOperatorStep() && !guidance.has_value() )
  {
    explanation.trustNotes.push_back( "本算子暂无编写指引（authored guidance）" );
  }

  // --- purpose: machine fact first, authored narrative second
  if ( facts.has_value() && !facts->purpose.empty() )
  {
    GroundedText entry;
    entry.text = facts->purpose;
    entry.provenance = FactProvenance::SystemFact;
    entry.evidence = { operatorSchemaLink( facts->id ) };
    explanation.purpose.push_back( entry );
  }
  if ( guidance.has_value() && !guidance->purpose.empty() )
  {
    GroundedText entry;
    entry.text = guidance->purpose;
    entry.provenance = FactProvenance::AuthoredGuidance;
    entry.evidence = { guidanceLink( request.operatorId ) };
    explanation.purpose.push_back( entry );
  }
  if ( explanation.purpose.empty() && !explanation.stepTitle.empty() )
  {
    explanation.trustNotes.push_back( "无已编写的目的说明；仅步骤标题可用" );
  }

  // --- prerequisites: machine + authored
  if ( facts.has_value() )
  {
    for ( const std::string &prerequisite : facts->prerequisites )
    {
      GroundedText entry;
      entry.text = prerequisite;
      entry.provenance = FactProvenance::SystemFact;
      entry.evidence = { operatorSchemaLink( facts->id ) };
      explanation.prerequisites.push_back( entry );
    }
  }
  if ( guidance.has_value() )
  {
    for ( const std::string &prerequisite : guidance->prerequisitesNote )
    {
      GroundedText entry;
      entry.text = prerequisite;
      entry.provenance = FactProvenance::AuthoredGuidance;
      entry.evidence = { guidanceLink( request.operatorId ) };
      explanation.prerequisites.push_back( entry );
    }
    for ( const std::string &assumption : guidance->assumptions )
    {
      GroundedText entry;
      entry.text = assumption;
      entry.provenance = FactProvenance::AuthoredGuidance;
      entry.evidence = { guidanceLink( request.operatorId ) };
      explanation.scientificAssumptions.push_back( entry );
    }
  }

  // --- state synthesis from port facts (only what is actually declared)
  auto synthesize = [ & ]( const char *aspect,
                           const std::function<std::string( const PortFactView & )> &extract,
                           const char *unitNote )
  {
    std::string before;
    std::string after;
    const bool hasBefore = collectPortValues( request.inputPorts, extract, before );
    const bool hasAfter = collectPortValues( request.outputPorts, extract, after );
    if ( !hasBefore && !hasAfter )
      return;
    StateTransition transition;
    transition.aspect = aspect;
    transition.before = hasBefore ? before : std::string();
    transition.after = hasAfter ? after : std::string();
    transition.provenance = FactProvenance::InferredExplanation;
    transition.evidence = { documentLink };
    if ( before == "mixed" || after == "mixed" )
      explanation.trustNotes.push_back(
        std::string( aspect ) + ": 输入/输出端口声明不一致，标记为 mixed" );
    if ( hasBefore && hasAfter && before == after )
      transition.explanation = std::string( "输入输出" ) + unitNote + "相同（该步骤在此配置下未改变此属性）";
    else
      transition.explanation = std::string( "由工作流端口的声明合成（" ) + unitNote + "）";
    explanation.stateChanges.push_back( transition );
  };

  synthesize( AspectRadiometricState,
              []( const PortFactView &p ) { return p.stateToken; }, "辐射状态" );
  synthesize( AspectCrs, []( const PortFactView &p ) { return p.crs; }, "CRS" );
  synthesize( AspectResolution,
              []( const PortFactView &p )
              {
                if ( !p.resolutionX.has_value() || !p.resolutionY.has_value() )
                  return std::string();
                return numberToString( *p.resolutionX ) + " x " + numberToString( *p.resolutionY );
              },
              "分辨率" );
  synthesize( AspectBandCount,
              []( const PortFactView &p )
              { return p.bandCount.has_value() ? std::to_string( *p.bandCount ) : std::string(); },
              "波段数" );

  // --- authored state narrative: fills gaps, never overrides facts
  if ( guidance.has_value() && guidance->stateNarrative.has_value() )
  {
    const AuthoredStateNarrative &narrative = *guidance->stateNarrative;
    StateTransition *synthesized = nullptr;
    for ( StateTransition &transition : explanation.stateChanges )
    {
      if ( transition.aspect == AspectRadiometricState )
        synthesized = &transition;
    }

    if ( synthesized != nullptr )
    {
      const bool contradicts = ( !narrative.before.empty() && !synthesized->before.empty() &&
                                 synthesized->before != "mixed" && narrative.before != synthesized->before ) ||
                               ( !narrative.after.empty() && !synthesized->after.empty() &&
                                 synthesized->after != "mixed" && narrative.after != synthesized->after );
      if ( contradicts )
      {
        outcome.problems.push_back(
          { "state_contradiction", "stateNarrative",
            "authored state narrative (" + narrative.before + "->" + narrative.after +
              ") contradicts declared port facts (" + synthesized->before + "->" +
              synthesized->after + "); port facts kept" } );
        explanation.trustNotes.push_back(
          "编写指引声称的状态变化（" + narrative.before + "→" + narrative.after +
          "）与端口声明不一致，以端口声明为准" );
      }
    }
    else
    {
      StateTransition transition;
      transition.aspect = AspectRadiometricState;
      transition.before = narrative.before;
      transition.after = narrative.after;
      transition.explanation = "来自编写指引的教学期望（无端口声明可合成）";
      transition.provenance = FactProvenance::AuthoredGuidance;
      transition.evidence = { guidanceLink( request.operatorId ) };
      explanation.stateChanges.push_back( transition );
    }
  }

  // --- parameter rationale: authored text validated against the live schema
  if ( guidance.has_value() && facts.has_value() )
  {
    for ( const AuthoredParameterRationale &authored : guidance->parameterRationale )
    {
      const auto it = std::find_if( facts->parameters.begin(), facts->parameters.end(),
                                    [ &authored ]( const ParamFact &p )
                                    { return p.name == authored.parameter; } );
      if ( it == facts->parameters.end() )
      {
        outcome.problems.push_back(
          { "unknown_parameter_reference", "parameterRationale.parameter",
            "authored rationale references parameter '" + authored.parameter +
              "' which does not exist in the live schema of " + facts->id } );
        continue;
      }
      ParameterRationale entry;
      entry.parameter = authored.parameter;
      if ( request.parameters.isObject() && request.parameters.isMember( authored.parameter ) )
      {
        entry.chosenValue = request.parameters[authored.parameter];
        entry.evidence.push_back( documentLink );
      }
      entry.rationale = authored.rationale;
      entry.misconfigurationConsequence = authored.misconfigurationConsequence;
      entry.provenance = FactProvenance::AuthoredGuidance;
      entry.evidence.push_back( guidanceLink( request.operatorId ) );
      explanation.parameterRationale.push_back( entry );
    }
  }

  // --- skip consequence: authored text; runtime skips are marked explicitly
  if ( guidance.has_value() && guidance->skipConsequence.has_value() )
  {
    SkippedStepConsequence entry;
    entry.summary = guidance->skipConsequence->summary;
    entry.detail = guidance->skipConsequence->detail;
    entry.downstreamRoles = guidance->skipConsequence->downstreamRoles;
    entry.provenance = FactProvenance::AuthoredGuidance;
    entry.evidence = { guidanceLink( request.operatorId ) };
    explanation.skipConsequence = entry;
  }
  if ( request.executionStatus == "Skipped" )
  {
    if ( explanation.skipConsequence.has_value() )
    {
      explanation.trustNotes.push_back( "本步骤在运行中被 Skipped：以上跳过后果即为本次运行实际发生的情况" );
    }
    else
    {
      explanation.trustNotes.push_back(
        "本步骤在运行中被 Skipped（父节点失败/取消级联）；未编写跳过后果说明，不推测其影响" );
    }
  }

  // --- execution evidence: machine links required, otherwise refused
  if ( evidence_ != nullptr && !request.runId.empty() )
  {
    const std::optional<StepEvidence> stepEvidence = evidence_->evidenceFor( request.runId, request.stepId );
    if ( stepEvidence.has_value() )
    {
      const bool hasMachine = std::any_of( stepEvidence->links.begin(), stepEvidence->links.end(),
                                           []( const EvidenceLink &l ) { return isMachineEvidenceKind( l.kind ); } );
      if ( hasMachine )
      {
        ExecutionFacts executionEntry;
        executionEntry.status = stepEvidence->status;
        executionEntry.elapsedMs = stepEvidence->elapsedMs;
        executionEntry.cacheHit = stepEvidence->cacheHit;
        executionEntry.artifactPath = stepEvidence->artifactPath;
        executionEntry.artifactDigest = stepEvidence->artifactDigest;
        executionEntry.startedUtc = stepEvidence->startedUtc;
        executionEntry.endedUtc = stepEvidence->endedUtc;
        executionEntry.errorMessage = stepEvidence->errorMessage;
        executionEntry.evidence = stepEvidence->links;
        explanation.execution = executionEntry;
        for ( const EvidenceLink &l : stepEvidence->links )
        {
          if ( std::none_of( explanation.evidenceLinks.begin(), explanation.evidenceLinks.end(),
                             [ &l ]( const EvidenceLink &existing ) { return existing.kind == l.kind && existing.target == l.target; } ) )
            explanation.evidenceLinks.push_back( l );
        }
      }
      else
      {
        outcome.problems.push_back( { "ungrounded_execution_evidence", "execution",
                                      "execution evidence carried no machine-kind link; refused" } );
      }
    }
  }

  // --- source references (authored citations)
  if ( guidance.has_value() )
  {
    for ( const AuthoredReference &authored : guidance->references )
    {
      SourceReference reference;
      reference.title = authored.title;
      reference.kind = authored.kind;
      reference.locator = authored.locator;
      reference.note = authored.note;
      explanation.sourceReferences.push_back( reference );
    }
  }

  return outcome;
}

} // namespace sicnu::explain
