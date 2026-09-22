#include "preflight/report.h"

#include "preflight/sha256.h"

namespace sicnu::preflight {
namespace {

bool isValidMode( const std::string &mode )
{
  return mode == "teaching" || mode == "agent";
}

} // namespace

Json::Value PreflightBudgets::toJson() const
{
  Json::Value j( Json::objectValue );
  j["max_rules"] = maxRules;
  j["max_inputs"] = maxInputs;
  j["max_findings"] = maxFindings;
  return j;
}

std::optional<PreflightBudgets> PreflightBudgets::fromJson( const Json::Value &json )
{
  if ( !json.isObject() )
    return std::nullopt;
  if ( !json["max_rules"].isInt() || !json["max_inputs"].isInt()
       || !json["max_findings"].isInt() )
    return std::nullopt;
  PreflightBudgets b;
  b.maxRules = json["max_rules"].asInt();
  b.maxInputs = json["max_inputs"].asInt();
  b.maxFindings = json["max_findings"].asInt();
  if ( b.maxRules <= 0 || b.maxInputs <= 0 || b.maxFindings <= 0 )
    return std::nullopt;
  return b;
}

bool isValidEvaluatedOutcome( const std::string &outcome )
{
  return outcome == "pass" || outcome == "finding" || outcome == "insufficient_facts"
         || outcome == "skipped";
}

Json::Value PreflightEvaluatedRule::toJson() const
{
  Json::Value j( Json::objectValue );
  j["rule_id"] = ruleId;
  j["revision"] = revision;
  j["outcome"] = outcome;
  j["detail"] = detail;
  return j;
}

std::optional<PreflightEvaluatedRule> PreflightEvaluatedRule::fromJson( const Json::Value &json )
{
  if ( !json.isObject() || !json["rule_id"].isString() || !json["outcome"].isString() )
    return std::nullopt;
  if ( !isValidEvaluatedOutcome( json["outcome"].asString() ) )
    return std::nullopt;
  PreflightEvaluatedRule e;
  e.ruleId = json["rule_id"].asString();
  e.revision = json["revision"].isInt() ? json["revision"].asInt() : 1;
  e.outcome = json["outcome"].asString();
  e.detail = json["detail"].isString() ? json["detail"].asString() : std::string();
  return e;
}

PreflightReport PreflightReport::makeEmpty( const std::string &operatorId,
                                            const std::string &mode )
{
  PreflightReport r;
  r.operatorId = operatorId;
  r.mode = mode;
  return r;
}

Json::Value PreflightReport::toJson() const
{
  Json::Value j( Json::objectValue );
  j["schema"] = schema;
  j["schema_version"] = schemaVersion;
  j["kind"] = kind;
  j["request_digest"] = requestDigest;
  j["rules_revision"] = rulesRevision;
  j["operator_id"] = operatorId;
  j["mode"] = mode;
  j["verdict"] = verdict;

  Json::Value fs( Json::arrayValue );
  for ( const PreflightFinding &f : findings )
    fs.append( f.toJson() );
  j["findings"] = fs;

  Json::Value ev( Json::arrayValue );
  for ( const PreflightEvaluatedRule &e : evaluated )
    ev.append( e.toJson() );
  j["evaluated"] = ev;

  j["budgets"] = budgets.toJson();
  return j;
}

std::optional<PreflightReport> PreflightReport::fromJson( const Json::Value &json )
{
  if ( !json.isObject() )
    return std::nullopt;
  if ( !json["schema"].isString() || json["schema"].asString() != "sicnu.preflight.report/1" )
    return std::nullopt;
  if ( !json["schema_version"].isString() || json["schema_version"].asString() != "1" )
    return std::nullopt;
  if ( !json["kind"].isString() || json["kind"].asString() != "preflight_report" )
    return std::nullopt;
  if ( !json["mode"].isString() || !isValidMode( json["mode"].asString() ) )
    return std::nullopt;
  if ( !json["verdict"].isString() || !isValidVerdict( json["verdict"].asString() ) )
    return std::nullopt;
  if ( !json["findings"].isArray() || !json["evaluated"].isArray() )
    return std::nullopt;

  auto budgets = PreflightBudgets::fromJson( json["budgets"] );
  if ( !budgets )
    return std::nullopt;

  PreflightReport r;
  r.schema = json["schema"].asString();
  r.schemaVersion = json["schema_version"].asString();
  r.kind = json["kind"].asString();
  r.requestDigest =
    json["request_digest"].isString() ? json["request_digest"].asString() : std::string();
  r.rulesRevision =
    json["rules_revision"].isString() ? json["rules_revision"].asString() : std::string();
  r.operatorId =
    json["operator_id"].isString() ? json["operator_id"].asString() : std::string();
  r.mode = json["mode"].asString();
  r.verdict = json["verdict"].asString();
  r.budgets = *budgets;

  for ( const auto &fj : json["findings"] )
  {
    auto f = PreflightFinding::fromJson( fj );
    if ( !f )
      return std::nullopt;
    r.findings.push_back( std::move( *f ) );
  }
  for ( const auto &ej : json["evaluated"] )
  {
    auto e = PreflightEvaluatedRule::fromJson( ej );
    if ( !e )
      return std::nullopt;
    r.evaluated.push_back( std::move( *e ) );
  }
  return r;
}

std::string canonicalReportJson( const PreflightReport &report )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  builder["precision"] = 12;
  builder["precisionType"] = "significant";
  builder["emitUTF8"] = true;
  return Json::writeString( builder, report.toJson() );
}

std::string reportDigest( const PreflightReport &report )
{
  return sha256Hex( canonicalReportJson( report ) );
}

} // namespace sicnu::preflight
