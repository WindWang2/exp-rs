#include "explain/step_explanation_view_model.h"

#include <json/json.h>

#include <algorithm>

namespace sicnu::explain
{
namespace
{

std::string badgeFor( FactProvenance provenance )
{
  switch ( provenance )
  {
    case FactProvenance::SystemFact:
      return "系统事实";
    case FactProvenance::AuthoredGuidance:
      return "编写指引";
    case FactProvenance::InferredExplanation:
      return "推断";
  }
  return {};
}

std::string evidenceNoteFor( const std::vector<EvidenceLink> &evidence )
{
  std::string note;
  for ( const EvidenceLink &link : evidence )
  {
    if ( !note.empty() )
      note += "; ";
    note += link.target;
  }
  return note;
}

std::string valueToText( const Json::Value &value )
{
  if ( value.isNull() )
    return "（未绑定）";
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, value );
}

void appendSection( std::vector<ExplanationSection> &sections, const char *id, const char *title,
                    std::vector<ExplanationLine> lines )
{
  if ( lines.empty() )
    return;
  sections.push_back( { id, title, std::move( lines ) } );
}

} // namespace

StepExplanationViewModel StepExplanationViewModel::fromExplanation(
  const StepExplanation &explanation )
{
  StepExplanationViewModel model;
  model.headline = explanation.stepId;
  if ( !explanation.stepTitle.empty() && explanation.stepTitle != explanation.stepId )
    model.headline += " — " + explanation.stepTitle;
  if ( !explanation.operatorId.empty() )
    model.operatorLine = explanation.operatorId +
                         ( explanation.operatorDisplayName.empty()
                             ? ""
                             : " (" + explanation.operatorDisplayName + ")" );

  std::vector<ExplanationLine> lines;
  for ( const GroundedText &entry : explanation.purpose )
  {
    lines.push_back( { entry.text, badgeFor( entry.provenance ), evidenceNoteFor( entry.evidence ) } );
  }
  appendSection( model.sections, "why", "为什么做（Why）", lines );

  lines.clear();
  for ( const GroundedText &entry : explanation.prerequisites )
  {
    lines.push_back( { entry.text, badgeFor( entry.provenance ), evidenceNoteFor( entry.evidence ) } );
  }
  appendSection( model.sections, "prerequisites", "前置条件（Prerequisites）", lines );

  lines.clear();
  for ( const GroundedText &entry : explanation.scientificAssumptions )
  {
    lines.push_back( { entry.text, badgeFor( entry.provenance ), evidenceNoteFor( entry.evidence ) } );
  }
  appendSection( model.sections, "assumptions", "科学假设（Assumptions）", lines );

  lines.clear();
  for ( const StateTransition &transition : explanation.stateChanges )
  {
    std::string text = transition.aspect + ": " +
                       ( transition.before.empty() ? "（未声明）" : transition.before ) + " → " +
                       ( transition.after.empty() ? "（未声明）" : transition.after );
    if ( !transition.explanation.empty() )
      text += "（" + transition.explanation + "）";
    lines.push_back( { text, badgeFor( transition.provenance ), evidenceNoteFor( transition.evidence ) } );
  }
  appendSection( model.sections, "state", "前后状态（What changed）", lines );

  lines.clear();
  for ( const ParameterRationale &rationale : explanation.parameterRationale )
  {
    std::string text = rationale.parameter + " = " + valueToText( rationale.chosenValue ) +
                       " — " + rationale.rationale;
    if ( !rationale.misconfigurationConsequence.empty() )
      text += "（配错后果: " + rationale.misconfigurationConsequence + "）";
    lines.push_back( { text, badgeFor( rationale.provenance ), evidenceNoteFor( rationale.evidence ) } );
  }
  appendSection( model.sections, "parameters", "参数理由（Parameter rationale）", lines );

  lines.clear();
  if ( explanation.skipConsequence.has_value() )
  {
    lines.push_back( { explanation.skipConsequence->summary, badgeFor( explanation.skipConsequence->provenance ),
                       evidenceNoteFor( explanation.skipConsequence->evidence ) } );
    if ( !explanation.skipConsequence->detail.empty() )
      lines.push_back( { explanation.skipConsequence->detail, {}, {} } );
    if ( !explanation.skipConsequence->downstreamRoles.empty() )
    {
      std::string roles;
      for ( const std::string &role : explanation.skipConsequence->downstreamRoles )
      {
        if ( !roles.empty() )
          roles += ", ";
        roles += role;
      }
      lines.push_back( { "受影响下游: " + roles, {}, {} } );
    }
  }
  appendSection( model.sections, "skip", "如果跳过（Skip consequence）", lines );

  lines.clear();
  if ( explanation.execution.has_value() )
  {
    std::string text = "状态: " + explanation.execution->status;
    if ( explanation.execution->elapsedMs.has_value() )
      text += "，耗时 " + std::to_string( *explanation.execution->elapsedMs ) + " ms";
    if ( explanation.execution->cacheHit.has_value() && *explanation.execution->cacheHit )
      text += "（缓存命中）";
    if ( !explanation.execution->artifactDigest.empty() )
      text += "，产物指纹 " + explanation.execution->artifactDigest;
    if ( !explanation.execution->errorMessage.empty() )
      text += "，错误: " + explanation.execution->errorMessage;
    lines.push_back( { text, badgeFor( FactProvenance::SystemFact ),
                       evidenceNoteFor( explanation.execution->evidence ) } );
  }
  appendSection( model.sections, "execution", "执行情况（Execution）", lines );

  lines.clear();
  for ( const SourceReference &reference : explanation.sourceReferences )
  {
    lines.push_back( { reference.title + " [" + reference.kind + "]: " + reference.locator, {}, {} } );
  }
  appendSection( model.sections, "sources", "来源（Sources）", lines );

  model.trustNotes = explanation.trustNotes;
  return model;
}

std::string StepExplanationViewModel::toMarkdown() const
{
  std::string markdown = "# " + headline + "\n";
  if ( !operatorLine.empty() )
    markdown += "operator: " + operatorLine + "\n";
  for ( const ExplanationSection &section : sections )
  {
    markdown += "\n## " + section.titleZh + "\n";
    for ( const ExplanationLine &line : section.lines )
    {
      markdown += "- ";
      if ( !line.badge.empty() )
        markdown += "[" + line.badge + "] ";
      markdown += line.text;
      if ( !line.evidenceNote.empty() )
        markdown += "（证据: " + line.evidenceNote + "）";
      markdown += "\n";
    }
  }
  if ( !trustNotes.empty() )
  {
    markdown += "\n## 注意（Trust notes）\n";
    for ( const std::string &note : trustNotes )
      markdown += "- " + note + "\n";
  }
  return markdown;
}

} // namespace sicnu::explain
