#include "teaching/lab_step_timeline.h"

namespace sicnu::teaching {
namespace {

std::string strOf( const Json::Value &v, const char *key )
{
  if ( !v.isObject() || !v.isMember( key ) || !v[key].isString() ) return {};
  return v[key].asString();
}

std::string pickZh( const Json::Value &v, const char *zhKey, const char *enKey )
{
  std::string zh = strOf( v, zhKey );
  if ( !zh.empty() ) return zh;
  return strOf( v, enKey );
}

bool isHumanKind( const std::string &kind, const std::string &boundary, const std::string &action )
{
  if ( kind == "human_only" || kind == "reflection" || kind == "manual" ) return true;
  if ( !action.empty() ) return true;
  if ( boundary == "ui_action" || boundary == "manual" || boundary == "reflection"
       || boundary == "judgment" )
    return true;
  return false;
}

Json::Value maskObject( const Json::Value &params, const std::vector<std::string> &keys )
{
  if ( !params.isObject() || keys.empty() ) return params;
  Json::Value out = params;
  for ( const auto &k : keys ) {
    if ( out.isMember( k ) ) out[k] = "***";
  }
  return out;
}

} // namespace

const LabStepItem *LabStepTimeline::current() const
{
  if ( currentIndex < 0 || currentIndex >= static_cast<int>( steps.size() ) ) return nullptr;
  return &steps[static_cast<size_t>( currentIndex )];
}

const LabStepItem *LabStepTimeline::previous() const
{
  if ( currentIndex <= 0 || steps.empty() ) return nullptr;
  return &steps[static_cast<size_t>( currentIndex - 1 )];
}

const LabStepItem *LabStepTimeline::next() const
{
  if ( currentIndex < 0 || currentIndex + 1 >= static_cast<int>( steps.size() ) ) return nullptr;
  return &steps[static_cast<size_t>( currentIndex + 1 )];
}

LabStepTimeline LabStepTimeline::fromLabSpec( const Json::Value &labSpec, int currentIndex )
{
  LabStepTimeline tl;
  if ( !labSpec.isObject() ) {
    tl.issuesZh.push_back( "LabSpec 缺失" );
    return tl;
  }
  tl.labId = strOf( labSpec, "id" );
  tl.titleZh = pickZh( labSpec, "title_zh", "title" );
  tl.objectiveZh = pickZh( labSpec, "objective_zh", "objective" );
  tl.sourceKind = "labspec";
  const auto &arr = labSpec["steps"];
  if ( !arr.isArray() || arr.empty() ) {
    tl.issuesZh.push_back( "LabSpec 无 steps" );
    return tl;
  }
  int i = 0;
  for ( const auto &s : arr ) {
    LabStepItem item;
    item.index = i;
    item.stepId = strOf( s, "id" );
    if ( item.stepId.empty() ) item.stepId = "step_" + std::to_string( i );
    item.title = strOf( s, "title" );
    item.titleZh = pickZh( s, "title_zh", "title" );
    item.descriptionZh = pickZh( s, "description_zh", "description" );
    item.operatorId = strOf( s, "operator_id" );
    item.params = s.isMember( "params" ) ? s["params"] : Json::Value( Json::objectValue );
    item.paramsDisplay = item.params;
    const std::string action = strOf( s, "action" );
    item.teachingNote = strOf( s, "teaching_note" );
    item.completionHint = strOf( s, "completion_hint" );
    item.commonErrorsZh = strOf( s, "common_errors_zh" );
    item.whyHintZh = item.teachingNote;
    if ( !item.operatorId.empty() ) {
      item.kind = "operator";
      item.humanRequired = false;
    } else if ( !action.empty() ) {
      item.kind = "ui_action";
      item.boundary = "ui_action";
      item.humanRequired = true;
    } else {
      item.kind = "manual";
      item.boundary = "manual";
      item.humanRequired = true;
    }
    // Reflection heuristic: thinking / reflection in id or title.
    const std::string lower = item.stepId + item.title;
    if ( strOf( s, "kind" ) == "reflection"
         || lower.find( "reflect" ) != std::string::npos
         || lower.find( "thinking" ) != std::string::npos ) {
      if ( strOf( s, "kind" ) == "reflection" ) {
        item.kind = "reflection";
        item.boundary = "reflection";
        item.humanRequired = true;
      }
    }
    if ( s.isMember( "kind" ) && s["kind"].isString() ) {
      item.kind = s["kind"].asString();
      item.humanRequired = isHumanKind( item.kind, item.boundary, action );
    }
    tl.steps.push_back( std::move( item ) );
    ++i;
  }
  if ( currentIndex < 0 || currentIndex >= static_cast<int>( tl.steps.size() ) )
    tl.currentIndex = 0;
  else
    tl.currentIndex = currentIndex;
  tl.ok = true;
  return tl;
}

LabStepTimeline LabStepTimeline::fromScientificRecipe( const Json::Value &recipe, int currentIndex )
{
  LabStepTimeline tl;
  if ( !recipe.isObject() ) {
    tl.issuesZh.push_back( "ScientificRecipe 缺失" );
    return tl;
  }
  if ( strOf( recipe, "schema" ) != "sicnu.scientific_recipe/1" ) {
    tl.issuesZh.push_back( "非 scientific_recipe/1 schema" );
    return tl;
  }
  tl.sourceKind = "scientific_recipe";
  tl.labId = strOf( recipe, "recipe_id" );
  if ( tl.labId.rfind( "lab.", 0 ) == 0 ) tl.labId = tl.labId.substr( 4 );
  tl.titleZh = pickZh( recipe, "title_zh", "title" );
  if ( recipe.isMember( "goal_pattern" ) )
    tl.objectiveZh = strOf( recipe["goal_pattern"], "intent" );
  const auto &stages = recipe["stages"];
  if ( !stages.isArray() || stages.empty() ) {
    tl.issuesZh.push_back( "ScientificRecipe 无 stages" );
    return tl;
  }
  int i = 0;
  for ( const auto &s : stages ) {
    LabStepItem item;
    item.index = i;
    item.stepId = strOf( s, "id" );
    if ( item.stepId.empty() ) item.stepId = "s" + std::to_string( i );
    item.titleZh = pickZh( s, "title_zh", "title" );
    item.title = strOf( s, "title" );
    item.kind = strOf( s, "kind" );
    if ( item.kind.empty() ) item.kind = "operator";
    item.operatorId = strOf( s, "operator_id" );
    item.params = s.isMember( "params" ) ? s["params"] : Json::Value( Json::objectValue );
    item.paramsDisplay = item.params;
    item.boundary = strOf( s, "boundary" );
    item.teachingNote = strOf( s, "note_zh" );
    if ( item.teachingNote.empty() ) item.teachingNote = strOf( s, "note" );
    item.whyHintZh = item.teachingNote;
    item.humanRequired = isHumanKind( item.kind, item.boundary, "" );
    if ( item.kind == "reflection" ) {
      item.completionHint = strOf( s, "prompt" );
      if ( item.completionHint.empty() ) item.completionHint = strOf( s, "prompt_zh" );
    }
    tl.steps.push_back( std::move( item ) );
    ++i;
  }
  if ( currentIndex < 0 || currentIndex >= static_cast<int>( tl.steps.size() ) )
    tl.currentIndex = 0;
  else
    tl.currentIndex = currentIndex;
  tl.ok = true;
  return tl;
}

LabStepTimeline LabStepTimeline::fromLabDocument( const Json::Value &doc, int currentIndex )
{
  if ( doc.isObject() && strOf( doc, "schema" ) == "sicnu.scientific_recipe/1" )
    return fromScientificRecipe( doc, currentIndex );
  if ( doc.isObject() && ( doc.isMember( "steps" ) || doc.isMember( "spec_version" ) ) )
    return fromLabSpec( doc, currentIndex );
  LabStepTimeline tl;
  tl.sourceKind = "unknown";
  tl.issuesZh.push_back( "无法识别为 LabSpec 或 ScientificRecipe（fail-closed）" );
  return tl;
}

void LabStepTimeline::applyTeachingMask( const std::vector<std::string> &studentDecisionKeys )
{
  for ( auto &s : steps ) {
    s.paramsStudentDecision = false;
    for ( const auto &k : studentDecisionKeys ) {
      if ( s.params.isObject() && s.params.isMember( k ) ) {
        s.paramsStudentDecision = true;
        break;
      }
    }
    s.paramsDisplay = maskObject( s.params, studentDecisionKeys );
  }
}

Json::Value LabStepTimeline::toJson() const
{
  Json::Value root( Json::objectValue );
  root["schema"] = schema;
  root["ok"] = ok;
  root["lab_id"] = labId;
  root["title_zh"] = titleZh;
  root["objective_zh"] = objectiveZh;
  root["source_kind"] = sourceKind;
  root["current_index"] = currentIndex;
  Json::Value issues( Json::arrayValue );
  for ( const auto &i : issuesZh ) issues.append( i );
  root["issues_zh"] = issues;
  Json::Value arr( Json::arrayValue );
  for ( const auto &s : steps ) {
    Json::Value o( Json::objectValue );
    o["step_id"] = s.stepId;
    o["index"] = s.index;
    o["title_zh"] = s.titleZh;
    o["title"] = s.title;
    o["description_zh"] = s.descriptionZh;
    o["kind"] = s.kind;
    o["operator_id"] = s.operatorId;
    o["params"] = s.params;
    o["params_display"] = s.paramsDisplay;
    o["boundary"] = s.boundary;
    o["teaching_note"] = s.teachingNote;
    o["completion_hint"] = s.completionHint;
    o["common_errors_zh"] = s.commonErrorsZh;
    o["why_hint_zh"] = s.whyHintZh;
    o["human_required"] = s.humanRequired;
    o["ai_allowed"] = s.aiAllowed;
    o["params_student_decision"] = s.paramsStudentDecision;
    o["execution_status"] = labUiStatusWire( s.executionStatus );
    o["execution_status_zh"] = labUiStatusLabelZh( s.executionStatus );
    o["execution_status_icon"] = labUiStatusIconToken( s.executionStatus );
    o["evidence_ref"] = s.evidenceRef;
    arr.append( o );
  }
  root["steps"] = arr;
  return root;
}

} // namespace sicnu::teaching
