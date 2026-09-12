// src/agent/harness/lab_copilot.cpp
#include "lab_copilot.h"

#include "harness_actions.h"
#include "harness_error.h"
#include "lab_diagnostics.h"
#include "lab_glossary.h"
#include "lab_intent.h"
#include "lab_spec.h"

namespace sicnu::agent::harness {

namespace {

constexpr const char *kRefusalReasonZh =
  "教学辅导模式不会替你完成实验或给出成绩：我可以帮你诊断结果为什么异常、提示下一步、讲解概念。";
constexpr const char *kRefusalAlternativeZh =
  "告诉我你卡在哪一步、看到了什么现象，我来帮你分析原因和下一步。";

Json::Value objectWithSuggestedActions()
{
  Json::Value v( Json::objectValue );
  v["suggested_actions"] = Json::Value( Json::arrayValue );
  return v;
}

/// Resolves one suggested action under the teaching gate and appends it.
/// This is the ONLY way an action enters a lab answer, so a student answer
/// cannot carry an artifact surface even if a producer misclassifies.
void appendGatedAction( Json::Value &doc, const std::string &key, Json::Value arguments,
                        const std::string &role )
{
  TeachingContext context;
  context.intentDomain = "lab";
  context.role = role;
  doc["suggested_actions"].append(
    resolvedSuggestedActionForRole( key, std::move( arguments ), context ) );
}

/// The lab context for one request: which lab, which step (0-based), and the
/// anchored step document (null when the student is not anchored).
struct LabAnchor
{
    std::string labId;
    std::string specStatus; ///< "ok" | "unspecified" | "unavailable" | "unknown_lab"
    int stepIndex = -1;     ///< 0-based; -1 unknown
    Json::Value step;       ///< stepDoc or null
};

LabAnchor resolveAnchor( const Json::Value &input, const std::string &message )
{
  LabAnchor anchor;
  anchor.labId = input.get( "lab_id", "" ).isString() ? input.get( "lab_id", "" ).asString() : "";
  LabSpecCatalog &catalog = LabSpecCatalog::instance();

  if ( anchor.labId.empty() )
  {
    anchor.specStatus = "unspecified";
    return anchor;
  }
  const Json::Value spec = catalog.lab( anchor.labId );
  if ( catalog.status() != "ok" )
  {
    anchor.specStatus = "unavailable";
    return anchor;
  }
  if ( spec.isNull() )
  {
    anchor.specStatus = "unknown_lab";
    return anchor;
  }
  anchor.specStatus = "ok";

  const int stepCount = static_cast<int>( spec["steps"].size() );
  // Explicit session step (1-based wire form) wins; the message is the fallback.
  int stepNumber = input.isMember( "current_step" ) && input["current_step"].isNumeric()
                     ? input["current_step"].asInt()
                     : 0;
  if ( stepNumber <= 0 )
  {
    const int fromMessage = LabSpecCatalog::stepIndexFromMessage( message, stepCount );
    if ( fromMessage >= 0 )
      anchor.stepIndex = fromMessage;
  }
  else if ( stepNumber <= stepCount )
  {
    anchor.stepIndex = stepNumber - 1;
  }
  if ( anchor.stepIndex >= 0 )
    anchor.step = catalog.stepDoc( anchor.labId, anchor.stepIndex );
  return anchor;
}

void appendAnchorDoc( Json::Value &result, const LabAnchor &anchor )
{
  Json::Value lab( Json::objectValue );
  lab["source"] = anchor.specStatus;
  if ( !anchor.labId.empty() )
    lab["id"] = anchor.labId;
  if ( !anchor.step.isNull() )
    lab["step"] = anchor.step;
  result["lab"] = std::move( lab );
}

/// Strips the interrogative frame from a concept question, leaving the term.
std::string conceptTermFromMessage( const std::string &message )
{
  static const char *const kPrefixes[] = { "请问什么是", "请问", "什么是", "请解释一下",
                                           "请解释", "解释一下", "解释下", "是什么意思",
                                           "什么意思", "介绍一下" };
  std::string term = message;
  for ( const char *prefix : kPrefixes )
  {
    const std::string p( prefix );
    if ( term.compare( 0, p.size(), p ) == 0 )
    {
      term = term.substr( p.size() );
      break;
    }
  }
  // Strip trailing question particles/punctuation.
  static const char *const kSuffixes[] = { "是什么意思", "什么意思", "是什么", "吗", "?", "？",
                                           "。", "，", ",", " " };
  bool trimmed = true;
  while ( trimmed && !term.empty() )
  {
    trimmed = false;
    for ( const char *suffix : kSuffixes )
    {
      const std::string s( suffix );
      if ( term.size() >= s.size() && term.compare( term.size() - s.size(), s.size(), s ) == 0 )
      {
        term.erase( term.size() - s.size() );
        trimmed = true;
      }
    }
  }
  return term;
}

Json::Value buildTroubleshootAnswer( Json::Value &result, const LabObservation &observation,
                                     const std::string &role )
{
  if ( !observation.present )
  {
    result["answer_zh"] =
      "要诊断问题，我需要看到实测数据：请先在数据检查面板读取该输出的统计值（最小/最大值、"
      "NoData 占比），或把两期数据的坐标系与像元大小发给我，再来找我分析。";
    return result;
  }

  const LabDiagnosis diagnosis = diagnoseLabObservation( observation );
  result["diagnosis"] = diagnosis.toJson();
  if ( !diagnosis.matched )
  {
    result["answer_zh"] =
      "你的观测数据里没有命中已知的错误特征。请先检查最基本的：数据是否加载成功、"
      "统计值是否正常（最小/最大值、NoData 占比），把结果发给我再深入分析。";
    return result;
  }

  result["answer_zh"] = "现象：" + diagnosis.symptomZh + " " + diagnosis.causeZh + " 下一步：" +
                        diagnosis.verifyZh + " 先只做这一步验证。";
  if ( !diagnosis.actionKey.empty() )
    appendGatedAction( result, diagnosis.actionKey, Json::Value(), role );

  // Cite the glossary term for the index when the seam knows it.
  if ( !observation.indexName.empty() )
  {
    const Json::Value entry = LabGlossary::instance().term( observation.indexName );
    if ( !entry.isNull() )
    {
      result["glossary_terms"] = Json::Value( Json::arrayValue );
      result["glossary_terms"].append( entry["zh"].asString() );
    }
  }
  return result;
}

Json::Value buildHintAnswer( Json::Value &result, const std::string &role,
                             const LabAnchor &anchor )
{
  if ( anchor.specStatus != "ok" || anchor.step.isNull() )
  {
    if ( anchor.specStatus == "unavailable" )
      result["answer_zh"] =
        "实验步骤数据（data/labs）当前不可用，我无法定位你所在的步骤。请直接描述你卡住的操作和现象，"
        "我来帮你分析。";
    else if ( anchor.specStatus == "unknown_lab" )
      result["answer_zh"] = "我找不到这个实验的步骤定义，请确认实验编号。";
    else
      result["answer_zh"] = "告诉我你正在做哪个实验、第几步，我给你针对这一步的提示（不是答案）。";
    return result;
  }

  const std::string title = anchor.step.get( "title_zh", "" ).asString();
  const std::string description = anchor.step.get( "description_zh", "" ).asString();
  std::string note = anchor.step.isMember( "teaching_note" )
                       ? anchor.step["teaching_note"].asString()
                       : std::string();
  if ( note.empty() && anchor.step.isMember( "completion_hint" ) )
    note = anchor.step["completion_hint"].asString();

  std::string answer = "你正在做第 " + std::to_string( anchor.step.get( "number", 0 ).asInt() ) +
                       " 步「" + title + "」。这一步要做的是：" + description;
  if ( anchor.step.isMember( "operator_id" ) )
  {
    const std::string operatorId = anchor.step["operator_id"].asString();
    answer += " 用到的算子是 " + operatorId + "，参数只需要填：";
    for ( const Json::Value &name : anchor.step["param_names"] )
      answer += name.asString() + "、";
    if ( !anchor.step["param_names"].empty() )
      answer.erase( answer.size() - 3 ); // drop the trailing 、
    answer += "。具体取值建议你自己从数据检查面板读出来。";
    Json::Value args( Json::objectValue );
    args["query"] = operatorId;
    appendGatedAction( result, "set_operator", std::move( args ), role );
  }
  if ( !note.empty() )
    answer += " 提示：" + note;
  result["answer_zh"] = std::move( answer );
  return result;
}

Json::Value buildConceptAnswer( Json::Value &result, const std::string &message )
{
  const std::string termText = conceptTermFromMessage( message );
  const LabGlossary &glossary = LabGlossary::instance();
  if ( termText.empty() )
  {
    result["answer_zh"] = "请告诉我要解释的术语（例如：什么是大气校正）。";
    return result;
  }
  if ( glossary.status() != "ok" )
  {
    result["answer_zh"] = "术语库（data/terms/rs_glossary.json）当前不可用，无法给出权威定义。"
                          "请先查看帮助面板中的对应主题。";
    result["lab_source_unavailable"] = "glossary";
    return result;
  }
  const Json::Value entry = glossary.term( termText );
  if ( entry.isNull() )
  {
    result["answer_zh"] =
      "术语库中没有收录「" + termText + "」。请确认术语写法，或查看帮助面板的搜索。";
    return result;
  }
  result["answer_zh"] = "「" + entry.get( "zh", "" ).asString() + "」（" +
                        entry.get( "en", "" ).asString() + "）：" +
                        entry.get( "definition_zh", "" ).asString();
  result["glossary_terms"] = Json::Value( Json::arrayValue );
  result["glossary_terms"].append( entry.get( "zh", "" ).asString() );
  if ( entry.isMember( "related" ) && entry["related"].isArray() )
    result["related_terms"] = entry["related"];
  return result;
}

} // namespace

bool labRoleMayUseTeacherSurfaces( const std::string &role )
{
  const std::string normalized = normalizeLabRole( role );
  return normalized == "teacher" || normalized == "admin";
}

Json::Value teachingRefusalEnvelope( const std::string &intent, const std::string &role )
{
  Json::Value details( Json::objectValue );
  details["intent"] = intent;
  details["role"] = role;
  details["alternative_zh"] = kRefusalAlternativeZh;
  // recoverable: the student can always re-ask as a hint/diagnosis request;
  // retry_class stays "none" — retrying the SAME request never succeeds.
  const HarnessError error = HarnessError::make( error_codes::kTeachingRefusal, kRefusalReasonZh,
                                                 std::move( details ), true,
                                                 Json::Value( Json::arrayValue ) );
  Json::Value envelope = errorEnvelope( error );

  Json::Value refusal( Json::objectValue );
  refusal["refused"] = true;
  refusal["intent"] = intent;
  refusal["role"] = role;
  refusal["reason_zh"] = kRefusalReasonZh;
  refusal["alternative_zh"] = kRefusalAlternativeZh;
  envelope["refusal"] = std::move( refusal );
  return envelope;
}

Json::Value labAsk( const Json::Value &input )
{
  // Role is session state. The message body is never consulted for authority.
  const std::string role = normalizeLabRole( input.get( "role", "" ).asString() );
  const std::string message = input.get( "message", "" ).asString();

  const LabIntentClassification classification = classifyLabIntent( message );
  std::string intent = classification.intent;

  // A bypass routed at the executor is execute-shaped, regardless of prose.
  const std::string routedTool =
    input.get( "routed_tool", "" ).isString() ? input.get( "routed_tool", "" ).asString() : "";
  if ( !routedTool.empty() )
    intent = kIntentLabExecute;

  // Teacher surfaces: grading and do-it-for-me execution. Students are
  // refused at the contract level — this branch IS the teaching constraint
  // applied to whole-request intents (the action-level twin lives in
  // harness_actions).
  if ( ( intent == kIntentLabExecute || intent == kIntentLabGradeRequest ) && isStudentRole( role ) )
    return teachingRefusalEnvelope( intent, role );

  const LabAnchor anchor = resolveAnchor( input, message );
  const LabObservation observation = parseLabObservation( input.get( "observation", Json::Value() ) );

  Json::Value result = objectWithSuggestedActions();
  result["intent"] = intent;
  result["role"] = role;
  Json::Value signals( Json::arrayValue );
  for ( const std::string &signal : classification.matchedSignals )
    signals.append( signal );
  result["classification_signals"] = std::move( signals );
  appendAnchorDoc( result, anchor );

  if ( intent == kIntentLabTroubleshoot )
    buildTroubleshootAnswer( result, observation, role );
  else if ( intent == kIntentLabHint )
    buildHintAnswer( result, role, anchor );
  else if ( intent == kIntentLabConcept )
    buildConceptAnswer( result, message );
  else // teacher requesting execution/grading in chat: point at the teacher surface.
    result["answer_zh"] = "请通过 harness:lab_reference 获取参考方案或成绩引用。";

  Json::Value envelope;
  envelope["success"] = true;
  envelope["result"] = std::move( result );
  return envelope;
}

Json::Value labReference( const Json::Value &input )
{
  const std::string role = normalizeLabRole( input.get( "role", "" ).asString() );
  const std::string kind = input.get( "kind", "" ).isString() ? input.get( "kind", "" ).asString() : "";
  const std::string labId = input.get( "lab_id", "" ).isString() ? input.get( "lab_id", "" ).asString() : "";
  if ( !labRoleMayUseTeacherSurfaces( role ) )
    return teachingRefusalEnvelope( kIntentLabExecute, role );

  Json::Value result = objectWithSuggestedActions();
  result["role"] = role;
  result["kind"] = kind;
  result["lab_id"] = labId;

  if ( kind == "reference_solution" )
  {
    LabSpecCatalog &catalog = LabSpecCatalog::instance();
    const Json::Value spec = catalog.lab( labId );
    if ( catalog.status() != "ok" || spec.isNull() )
    {
      Json::Value reference( Json::objectValue );
      reference["status"] = "unavailable";
      reference["reason_zh"] = "实验步骤数据（data/labs）当前不可用，无法生成参考方案。";
      result["reference"] = std::move( reference );
    }
    else
    {
      // Teachers get the full steps INCLUDING parameter values: this is the
      // reference solution the student must not receive.
      Json::Value reference( Json::objectValue );
      reference["status"] = "ok";
      reference["title_zh"] = spec.get( "title_zh", spec.get( "title", "" ) ).asString();
      reference["objective"] = spec.get( "objective", "" ).asString();
      reference["steps"] = spec["steps"];
      if ( spec.isMember( "thinking_questions" ) )
        reference["thinking_questions"] = spec["thinking_questions"];
      result["reference"] = std::move( reference );
    }
  }
  else if ( kind == "grade_citation" )
  {
    // D4 seam: the teaching grader (LabGradeResult) is not merged on this
    // branch. Degrade with a typed unavailable — never fabricate a score.
    Json::Value grade( Json::objectValue );
    grade["status"] = "unavailable";
    grade["seam"] = "LabGradeResult (D4 zcode/lab-auto-grading)";
    grade["reason_zh"] = "自动评分器尚未接入，无法引用成绩。";
    result["grade"] = std::move( grade );
  }
  else
  {
    const HarnessError error = HarnessError::make(
      error_codes::kInvalidParameter, "kind must be reference_solution or grade_citation" );
    return errorEnvelope( error );
  }

  Json::Value envelope;
  envelope["success"] = true;
  envelope["result"] = std::move( result );
  return envelope;
}

} // namespace sicnu::agent::harness
