#include "teaching/autonomy_effective_display.h"

#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_policy.h"
#include "agent/autonomy/autonomy_projection.h"

namespace sicnu::teaching {
namespace {

std::string strOf( const Json::Value &v, const char *key )
{
  if ( !v.isObject() || !v.isMember( key ) || !v[key].isString() ) return {};
  return v[key].asString();
}

const char *ladderZh( int ordinal )
{
  switch ( ordinal ) {
    case 0: return "L0 无辅助";
    case 1: return "L1 概念提示";
    case 2: return "L2 错误定位";
    case 3: return "L3 下一步建议";
    case 4: return "L4 计划生成";
    case 5: return "L5 自主执行";
    default: return "L?";
  }
}

void appendRows( std::vector<AutonomyCapabilityRow> &rows, const Json::Value &list,
                 const std::string &decision )
{
  if ( !list.isArray() ) return;
  for ( const auto &c : list ) {
    AutonomyCapabilityRow row;
    if ( c.isString() ) {
      row.capability = c.asString();
      row.decision = decision;
    } else if ( c.isObject() ) {
      row.capability = strOf( c, "capability" );
      row.decision = decision;
      row.reasonCode = strOf( c, "reason_code" );
      row.reasonZh = strOf( c, "reason_zh" );
      row.downgradeTo = strOf( c, "downgrade_to" );
    }
    if ( !row.capability.empty() ) rows.push_back( std::move( row ) );
  }
}

} // namespace

AutonomyEffectiveDisplay AutonomyEffectiveDisplay::fromStatusDoc( const Json::Value &statusDoc )
{
  AutonomyEffectiveDisplay d;
  for ( int i = 0; i <= 5; ++i ) d.ladderLabelsZh.push_back( ladderZh( i ) );
  if ( !statusDoc.isObject() ) {
    d.issuesZh.push_back( "autonomy-status 缺失" );
    return d;
  }
  if ( strOf( statusDoc, "schema" ) != sicnu::agent::autonomy::kAutonomyStatusSchema ) {
    d.issuesZh.push_back( "非 sicnu.autonomy-status/1" );
    return d;
  }
  d.effectiveLevel = strOf( statusDoc, "level" );
  if ( d.effectiveLevel.empty() ) d.effectiveLevel = strOf( statusDoc, "effective_level" );
  sicnu::agent::autonomy::AutonomyLevel lvl;
  if ( sicnu::agent::autonomy::autonomyLevelFromString( d.effectiveLevel, lvl ) )
    d.effectiveOrdinal = sicnu::agent::autonomy::autonomyLevelOrdinal( lvl );
  d.mode = strOf( statusDoc, "mode" );
  d.role = strOf( statusDoc, "role" );
  d.domain = strOf( statusDoc, "domain" );
  appendRows( d.rows, statusDoc["allowed"], "allow" );
  appendRows( d.rows, statusDoc["limited"], "limited" );
  appendRows( d.rows, statusDoc["forbidden"], "deny" );
  d.ok = true;
  return d;
}

AutonomyEffectiveDisplay AutonomyEffectiveDisplay::fromPolicyDoc( const Json::Value &policyDoc,
                                                                  const std::string &role,
                                                                  const std::string &domain )
{
  using namespace sicnu::agent::autonomy;
  auto parsed = parseAutonomyPolicy( policyDoc );
  if ( !parsed.ok ) {
    AutonomyEffectiveDisplay d;
    for ( int i = 0; i <= 5; ++i ) d.ladderLabelsZh.push_back( ladderZh( i ) );
    for ( const auto &e : parsed.errors ) d.issuesZh.push_back( e );
    if ( d.issuesZh.empty() ) d.issuesZh.push_back( "policy parse failed" );
    return d;
  }
  Json::Value status = autonomyStatusProjection( parsed.policy, role, domain );
  auto d = fromStatusDoc( status );
  d.role = role;
  d.domain = domain;
  return d;
}

Json::Value AutonomyEffectiveDisplay::requestCapability( const Json::Value &policyDoc,
                                                         const std::string &capability,
                                                         const std::string &role,
                                                         const std::string &domain ) const
{
  using namespace sicnu::agent::autonomy;
  Json::Value out( Json::objectValue );
  auto parsed = parseAutonomyPolicy( policyDoc );
  if ( !parsed.ok ) {
    out["decision"] = "deny";
    out["reason_code"] = "AUTONOMY_POLICY_INVALID";
    out["reason_zh"] = "策略无效（fail-closed）";
    return out;
  }
  AutonomyRequest req;
  req.capability = capability;
  req.role = role;
  req.domain = domain;
  const AutonomyDecision decision = decideAutonomy( parsed.policy, req );
  return autonomyDecisionDoc( decision );
}

Json::Value AutonomyEffectiveDisplay::toJson() const
{
  Json::Value root( Json::objectValue );
  root["schema"] = schema;
  root["ok"] = ok;
  root["effective_level"] = effectiveLevel;
  root["effective_ordinal"] = effectiveOrdinal;
  root["mode"] = mode;
  root["role"] = role;
  root["domain"] = domain;
  Json::Value ladder( Json::arrayValue );
  for ( const auto &l : ladderLabelsZh ) ladder.append( l );
  root["ladder_labels_zh"] = ladder;
  Json::Value issues( Json::arrayValue );
  for ( const auto &i : issuesZh ) issues.append( i );
  root["issues_zh"] = issues;
  Json::Value rows( Json::arrayValue );
  for ( const auto &r : this->rows ) {
    Json::Value o( Json::objectValue );
    o["capability"] = r.capability;
    o["decision"] = r.decision;
    o["reason_code"] = r.reasonCode;
    o["reason_zh"] = r.reasonZh;
    o["downgrade_to"] = r.downgradeTo;
    rows.append( o );
  }
  root["rows"] = rows;
  return root;
}

} // namespace sicnu::teaching
