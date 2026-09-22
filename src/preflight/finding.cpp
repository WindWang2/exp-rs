#include "preflight/finding.h"

#include <algorithm>

namespace sicnu::preflight {

std::string severityToString( PreflightSeverity severity )
{
  switch ( severity )
  {
    case PreflightSeverity::Block:
      return "block";
    case PreflightSeverity::RequireAck:
      return "require_ack";
    case PreflightSeverity::Warn:
      return "warn";
    case PreflightSeverity::Info:
      return "info";
  }
  return "info";
}

std::optional<PreflightSeverity> severityFromString( const std::string &text )
{
  if ( text == "block" ) return PreflightSeverity::Block;
  if ( text == "require_ack" ) return PreflightSeverity::RequireAck;
  if ( text == "warn" ) return PreflightSeverity::Warn;
  if ( text == "info" ) return PreflightSeverity::Info;
  return std::nullopt;
}

bool isValidFactBasis( const std::string &basis )
{
  return basis == "observed" || basis == "declared" || basis == "derived"
         || basis == "assumed" || basis == "unknown";
}

bool isValidVerdict( const std::string &verdict )
{
  return verdict == "ok" || verdict == "requires_ack" || verdict == "blocked";
}

Json::Value PreflightFinding::toJson() const
{
  Json::Value j( Json::objectValue );
  j["code"] = code;
  j["severity"] = severityToString( severity );
  j["rule_id"] = ruleId;
  j["rule_revision"] = ruleRevision;
  j["domain"] = domain;
  j["subject"] = subject;

  Json::Value inputs( Json::arrayValue );
  for ( const std::string &ref : affectedInputs )
    inputs.append( ref );
  j["affected_inputs"] = inputs;

  j["evidence"] = evidence.isObject() ? evidence : Json::Value( Json::objectValue );
  j["basis"] = basis;
  j["acknowledged"] = acknowledged;
  j["human_explanation"] = humanExplanation;
  j["machine_explanation"] =
    machineExplanation.isObject() ? machineExplanation : Json::Value( Json::objectValue );
  return j;
}

std::optional<PreflightFinding> PreflightFinding::fromJson( const Json::Value &json )
{
  if ( !json.isObject() )
    return std::nullopt;
  if ( !json["code"].isString() || json["code"].asString().empty() )
    return std::nullopt;
  if ( !json["severity"].isString() )
    return std::nullopt;
  const auto severity = severityFromString( json["severity"].asString() );
  if ( !severity )
    return std::nullopt;
  if ( !json["basis"].isString() || !isValidFactBasis( json["basis"].asString() ) )
    return std::nullopt;

  PreflightFinding f;
  f.code = json["code"].asString();
  f.severity = *severity;
  f.ruleId = json["rule_id"].isString() ? json["rule_id"].asString() : std::string();
  f.ruleRevision = json["rule_revision"].isInt() ? json["rule_revision"].asInt() : 1;
  f.domain = json["domain"].isString() ? json["domain"].asString() : std::string();
  f.subject = json["subject"].isString() ? json["subject"].asString() : std::string();
  if ( json["affected_inputs"].isArray() )
  {
    for ( const auto &ref : json["affected_inputs"] )
    {
      if ( !ref.isString() )
        return std::nullopt;
      f.affectedInputs.push_back( ref.asString() );
    }
  }
  f.evidence = json["evidence"].isObject() ? json["evidence"]
                                           : Json::Value( Json::objectValue );
  f.basis = json["basis"].asString();
  f.acknowledged = json["acknowledged"].isBool() && json["acknowledged"].asBool();
  f.humanExplanation =
    json["human_explanation"].isString() ? json["human_explanation"].asString() : std::string();
  f.machineExplanation = json["machine_explanation"].isObject()
                           ? json["machine_explanation"]
                           : Json::Value( Json::objectValue );
  return f;
}

bool findingLess( const PreflightFinding &a, const PreflightFinding &b )
{
  auto rank = []( PreflightSeverity s ) {
    switch ( s )
    {
      case PreflightSeverity::Block:
        return 0;
      case PreflightSeverity::RequireAck:
        return 1;
      case PreflightSeverity::Warn:
        return 2;
      case PreflightSeverity::Info:
        return 3;
    }
    return 4;
  };
  if ( rank( a.severity ) != rank( b.severity ) )
    return rank( a.severity ) < rank( b.severity );
  if ( a.code != b.code )
    return a.code < b.code;
  return a.subject < b.subject;
}

} // namespace sicnu::preflight
