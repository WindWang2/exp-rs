// src/agent/harness/workflow_explain.cpp
#include "workflow_explain.h"

#include <json/writer.h>

#include <algorithm>

namespace sicnu::agent::harness::explain {

namespace {

std::string clampZh( const std::string &text, Json::Value &truncatedKeys, const char *key )
{
  if ( static_cast<int>( text.size() ) <= ExplainLimits::kMaxTextChars )
    return text;
  truncatedKeys.append( key );
  return text.substr( 0, ExplainLimits::kMaxTextChars );
}

std::string canonicalJson( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, value );
}

/// Cause ordering: run failures first, then errors, then warnings-as-causes
/// (refusals), then auto repairs — the reader sees "what broke" before
/// "what was changed".
int causePriority( const std::string &kind )
{
  if ( kind == "run_failure" )
    return 0;
  if ( kind == "check_failed" )
    return 1;
  if ( kind == "refusal" )
    return 2;
  return 3; // auto_repair
}

} // namespace

Json::Value explainLimits()
{
  Json::Value limits( Json::objectValue );
  limits["max_causes"] = ExplainLimits::kMaxCauses;
  limits["max_text_chars"] = ExplainLimits::kMaxTextChars;
  limits["max_text_bytes"] = ExplainLimits::kMaxTextBytes;
  return limits;
}

std::string zhTemplateForCode( const std::string &code )
{
  // Closed zh-CN template table. Keys are the stable error codes; values are
  // one-liners a student can read without opening the ledger.
  if ( code == error_codes::kCrsMismatch )
    return "两个输入的坐标系不一致，需要重投影到统一 CRS";
  if ( code == error_codes::kGridMismatch )
    return "输入栅格的网格（尺寸/分辨率）不一致，需要重采样对齐";
  if ( code == error_codes::kWavelengthIncompatible )
    return "输入波段的波长与该算子的波段需求不符";
  if ( code == error_codes::kTemporalMisalignment )
    return "时间覆盖与算子的时序需求不符（景数不足或缺少获取时间）";
  if ( code == error_codes::kTemporalCalendarConflict )
    return "观测到的获取日期与计划声明的时间契约（日期范围/节奏）冲突";
  if ( code == error_codes::kNumericDomainChain )
    return "输入来自不同的辐射链路（如 DN 与地表反射率混用），数值域不一致";
  if ( code == error_codes::kBandIdentityMismatch )
    return "同一数据源被接入多个输入端口，而该算子需要不同的波段角色";
  if ( code == error_codes::kOutputIdentityMismatch )
    return "声明的输出类型与上游产物的类型不一致";
  if ( code == error_codes::kCategoricalMismatch )
    return "分类（类别值）输入被送入连续值算子";
  if ( code == error_codes::kResourceOverBudget )
    return "预估资源（内存/显存）超出计划的资源预算";
  if ( code == error_codes::kOutputPathCollision )
    return "多个输出写到了同一路径，先产生的文件会被覆盖";
  if ( code == error_codes::kNondeterministicChain )
    return "计划声明了确定性期望，但链路中包含可能非确定性的算子";
  if ( code == error_codes::kFactConflict )
    return "声明的属性与实测属性不一致，实测值优先";
  if ( code == error_codes::kModelIncompatible )
    return "模型输入契约与上游产物的波段/类型不匹配";
  if ( code == error_codes::kModelNotReady )
    return "模型未就绪（权重缺失或未注册）";
  if ( code == error_codes::kDatasetNotFound )
    return "数据集未能解析（不存在或不在工作区内）";
  if ( code == error_codes::kInvalidRadiometry )
    return "辐射状态不满足该算子的要求";
  if ( code == error_codes::kModalityMismatch )
    return "数据模态（光学/SAR/DEM）与算子需求不符";
  if ( code == error_codes::kPolarizationMismatch )
    return "SAR 极化方式与算子需求不符";
  if ( code == error_codes::kCalibrationMismatch )
    return "SAR 定标状态与算子需求不符";
  if ( code == error_codes::kExecutionFailed )
    return "执行阶段失败（详见运行记录）";
  if ( code == error_codes::kInsufficientMemory )
    return "内存不足，需要降低分辨率/分块或更换设备";
  if ( code == error_codes::kInvalidPlan )
    return "计划文档结构不合法";
  if ( code == error_codes::kInvalidParameter )
    return "参数不合法";
  return std::string();
}

Json::Value explainDecisionChain( const ExplainRequest &request )
{
  Json::Value out( Json::objectValue );
  out["schema_version"] = "1.0";
  Json::Value truncatedKeys( Json::arrayValue );
  std::vector<Json::Value> causes;

  int totalCauses = 0;

  // 1. The run's typed failure (when the caller came from a failed run).
  if ( !request.runErrorCode.empty() )
  {
    Json::Value cause( Json::objectValue );
    cause["kind"] = "run_failure";
    cause["code"] = request.runErrorCode;
    if ( !request.failedNode.empty() )
      cause["node"] = request.failedNode;
    const std::string zh = zhTemplateForCode( request.runErrorCode );
    cause["msg_zh"] = clampZh( zh.empty() ? "运行失败，错误码见 code 字段" : zh,
                               truncatedKeys, "run_failure.msg_zh" );
    causes.push_back( cause );
    ++totalCauses;
  }

  if ( request.analysis )
  {
    // 2. Analysis errors and warnings — the checks that failed and the facts
    //    they consumed. The fact basis of each issue is its details block
    //    (the analysis already echoes the consumed facts there).
    for ( const IrIssue &issue : request.analysis->issues )
    {
      if ( issue.severity != "error" && issue.severity != "warning" )
        continue;
      if ( issue.severity == "warning" )
        continue; // warnings are advisory; errors and refusals explain outcomes
      Json::Value cause( Json::objectValue );
      cause["kind"] = "check_failed";
      cause["code"] = issue.code;
      if ( !issue.node.empty() )
        cause["node"] = issue.node;
      if ( !issue.port.empty() )
        cause["port"] = issue.port;
      cause["msg_zh"] =
        clampZh( zhTemplateForCode( issue.code ).empty()
                   ? std::string( "静态检查未通过（" ) + issue.code + "）"
                   : zhTemplateForCode( issue.code ),
                 truncatedKeys, "check_failed.msg_zh" );
      cause["evidence"] = issue.details;
      causes.push_back( cause );
      ++totalCauses;
    }

    // 3. Refusals: decisions waiting on the caller.
    if ( request.refusals )
    {
      for ( const IrRefusal &refusal : *request.refusals )
      {
        Json::Value cause( Json::objectValue );
        cause["kind"] = "refusal";
        cause["rule_id"] = refusal.ruleId;
        if ( !refusal.issueCode.empty() )
          cause["code"] = refusal.issueCode;
        cause["msg_zh"] =
          clampZh( "科学含义会被改变的修复未自动插入，需要明确决策：" + refusal.why,
                   truncatedKeys, "refusal.msg_zh" );
        cause["missing_facts"] = refusal.missingFacts;
        causes.push_back( cause );
        ++totalCauses;
      }
    }

    // 4. Auto repairs: mutations that already happened (shape-preserving).
    if ( request.repairs )
    {
      for ( const IrRepairRecord &record : *request.repairs )
      {
        Json::Value cause( Json::objectValue );
        cause["kind"] = "auto_repair";
        cause["rule_id"] = record.ruleId;
        cause["risk"] = record.risk;
        if ( !record.insertedNode.empty() )
          cause["inserted_node"] = record.insertedNode;
        cause["msg_zh"] = clampZh(
          "已自动插入保持几何形态的修复节点（规则 " + record.ruleId + "），指纹随之改变",
          truncatedKeys, "auto_repair.msg_zh" );
        cause["facts_used"] = record.factsUsed;
        causes.push_back( cause );
        ++totalCauses;
      }
    }
  }

  // Deterministic order: kind priority, then code, then node.
  std::stable_sort( causes.begin(), causes.end(),
                    []( const Json::Value &a, const Json::Value &b ) {
                      const int pa = causePriority( a.get( "kind", "" ).asString() );
                      const int pb = causePriority( b.get( "kind", "" ).asString() );
                      if ( pa != pb )
                        return pa < pb;
                      if ( a.get( "code", "" ).asString() != b.get( "code", "" ).asString() )
                        return a.get( "code", "" ).asString() <
                               b.get( "code", "" ).asString();
                      return a.get( "node", "" ).asString() < b.get( "node", "" ).asString();
                    } );

  out["causes_total"] = totalCauses;
  int kept = 0;
  Json::Value causesJson( Json::arrayValue );
  for ( const Json::Value &cause : causes )
  {
    if ( kept++ >= ExplainLimits::kMaxCauses )
      break;
    causesJson.append( cause );
  }
  out["causes"] = causesJson;
  out["truncated"] = totalCauses > kept || !truncatedKeys.empty();
  if ( !truncatedKeys.empty() )
    out["truncated_keys"] = truncatedKeys;

  // Summary: one zh-CN line, honest about what dominates.
  std::string summary;
  if ( !request.runErrorCode.empty() )
    summary = "运行失败：" + zhTemplateForCode( request.runErrorCode ) +
              "。以下按因果顺序列出导致该结果的事实、契约与决策。";
  else if ( request.analysis && request.analysis->blocked() )
    summary = "编译被阻断：存在无法自动修复的契约冲突，请先处理下方错误项。";
  else if ( request.analysis && request.analysis->verdict == "fixable" )
    summary = "编译可修复：全部错误都有对应修复规则或待决策项。";
  else
    summary = "未发现阻断性事实或契约问题。";
  out["summary_zh"] = clampZh( summary, truncatedKeys, "summary_zh" );

  // Serialized-budget accounting: report honestly when the document exceeds
  // the declared budget (content is NOT cut mid-structure — the cap is a
  // declared bound, measured and reported, and callers can page causes).
  const int serializedBytes = static_cast<int>( canonicalJson( out ).size() );
  out["serialized_bytes"] = serializedBytes;
  if ( serializedBytes > ExplainLimits::kMaxTextBytes )
    out["over_budget"] = true;
  return out;
}

} // namespace sicnu::agent::harness::explain
