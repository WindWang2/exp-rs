#include "teaching/lab_feedback_projection.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace sicnu::teaching {
namespace {

std::string strOf( const Json::Value &v, const char *key )
{
  if ( !v.isObject() || !v.isMember( key ) || !v[key].isString() ) return {};
  return v[key].asString();
}

std::string lower( std::string s )
{
  for ( char &c : s ) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
  return s;
}

/// Normalize any status spelling; unknown → indeterminate (fail-closed).
std::string normalizeStatus( std::string raw )
{
  raw = lower( std::move( raw ) );
  if ( raw == "pass" || raw == "passed" || raw == "ok" ) return "pass";
  if ( raw == "fail" || raw == "failed" || raw == "error" ) return "fail";
  if ( raw == "indeterminate" || raw == "unverifiable" || raw == "unknown"
       || raw == "not_earned" || raw == "partial" || raw.empty() )
    return "indeterminate";
  // Explicitly refuse to treat anything else as pass.
  return "indeterminate";
}

const char *statusZh( const std::string &s )
{
  if ( s == "pass" ) return "通过";
  if ( s == "fail" ) return "未通过";
  return "不确定"; // indeterminate — never "通过"
}

bool countsAsPass( const std::string &s ) { return s == "pass"; }

std::string aggregate( const std::vector<std::string> &statuses )
{
  if ( statuses.empty() ) return "indeterminate";
  bool anyFail = false, anyIndet = false;
  for ( const auto &s : statuses ) {
    if ( s == "fail" ) anyFail = true;
    else if ( s != "pass" ) anyIndet = true;
  }
  if ( anyFail ) return "fail";
  if ( anyIndet ) return "indeterminate";
  return "pass";
}

} // namespace

LabFeedbackProjection LabFeedbackProjection::fromReports( const std::string &labId,
                                                          const Json::Value &verifierReport,
                                                          const Json::Value &graderReport,
                                                          const std::string &capsuleRef )
{
  LabFeedbackProjection p;
  p.labId = labId;
  p.capsuleExportRef = capsuleRef;
  std::vector<std::string> agg;

  if ( verifierReport.isObject() ) {
    std::string overall = strOf( verifierReport, "status" );
    if ( overall.empty() ) overall = strOf( verifierReport, "overall" );
    if ( overall.empty() && verifierReport.isMember( "verdict" ) )
      overall = strOf( verifierReport, "verdict" );
    overall = normalizeStatus( overall );
    // Empty report object with no status → indeterminate.
    if ( !verifierReport.isMember( "status" ) && !verifierReport.isMember( "overall" )
         && !verifierReport.isMember( "verdict" ) && !verifierReport.isMember( "checks" ) ) {
      overall = "indeterminate";
      p.issuesZh.push_back( "verifier 报告缺少 status（按不确定处理）" );
    }
    FeedbackCheckRow row;
    row.id = "verifier.overall";
    row.layer = "verifier";
    row.status = overall;
    row.statusZh = statusZh( overall );
    row.reasonZh = strOf( verifierReport, "reason_zh" );
    if ( row.reasonZh.empty() ) row.reasonZh = strOf( verifierReport, "summary" );
    row.countsAsPass = countsAsPass( overall );
    p.rows.push_back( row );
    agg.push_back( overall );
    p.techValidationSummaryZh = std::string( "技术验证: " ) + row.statusZh
                                + ( row.reasonZh.empty() ? "" : ( " — " + row.reasonZh ) );

    const auto &checks = verifierReport["checks"];
    if ( checks.isArray() ) {
      int i = 0;
      for ( const auto &c : checks ) {
        FeedbackCheckRow cr;
        cr.id = strOf( c, "id" );
        if ( cr.id.empty() ) cr.id = "verifier.check." + std::to_string( i );
        cr.layer = "verifier";
        cr.status = normalizeStatus( strOf( c, "status" ) );
        cr.statusZh = statusZh( cr.status );
        cr.reasonZh = strOf( c, "reason_zh" );
        cr.countsAsPass = countsAsPass( cr.status );
        // INVARIANT
        if ( cr.status == "indeterminate" ) cr.countsAsPass = false;
        p.rows.push_back( cr );
        agg.push_back( cr.status );
        ++i;
      }
    }
  } else {
    p.techValidationSummaryZh = "技术验证: 未提供（不确定）";
    agg.push_back( "indeterminate" );
  }

  if ( graderReport.isObject() ) {
    // Grader report schema sicnu.grader.report/1 — project outcomes without
    // leaking golden answers. Prefer criterion outcomes + reasons.
    std::string gStatus = strOf( graderReport, "overall" );
    if ( gStatus.empty() ) gStatus = strOf( graderReport, "verdict" );
    if ( gStatus.empty() ) gStatus = strOf( graderReport, "status" );
    // Map grader vocabulary: earned/not_earned/indeterminate
    const std::string gNorm = normalizeStatus( gStatus );
    FeedbackCheckRow grow;
    grow.id = "grader.overall";
    grow.layer = "grader";
    grow.status = gNorm;
    grow.statusZh = statusZh( gNorm );
    grow.reasonZh = strOf( graderReport, "summary_zh" );
    grow.countsAsPass = countsAsPass( gNorm );
    p.rows.push_back( grow );
    agg.push_back( gNorm );

    if ( graderReport.isMember( "score" ) ) {
      if ( graderReport["score"].isNumeric() ) {
        // sicnu.lab.grade/1 canonical body: score is 0..100 against a pass
        // line — project the scale, not a bare number.
        Json::Value sc( Json::objectValue );
        sc["score"] = graderReport["score"];
        if ( graderReport.isMember( "passing_score" ) )
          sc["passing_score"] = graderReport["passing_score"];
        if ( graderReport.isMember( "capped_by_blocking" ) )
          sc["capped_by_blocking"] = graderReport["capped_by_blocking"];
        p.graderScore = sc;
      } else {
        p.graderScore = graderReport["score"];
      }
    } else if ( graderReport.isMember( "earned_points" ) || graderReport.isMember( "max_points" ) ) {
      Json::Value sc( Json::objectValue );
      if ( graderReport.isMember( "earned_points" ) ) sc["earned"] = graderReport["earned_points"];
      if ( graderReport.isMember( "max_points" ) ) sc["max"] = graderReport["max_points"];
      p.graderScore = sc;
    }

    const auto &criteria = graderReport.isMember( "criteria" ) ? graderReport["criteria"]
                                                               : graderReport["outcomes"];
    if ( criteria.isArray() ) {
      int i = 0;
      for ( const auto &c : criteria ) {
        FeedbackCheckRow cr;
        cr.id = strOf( c, "id" );
        if ( cr.id.empty() ) cr.id = strOf( c, "criterion_id" );
        if ( cr.id.empty() ) cr.id = "grader.criterion." + std::to_string( i );
        cr.layer = "grader";
        std::string st = strOf( c, "outcome" );
        if ( st.empty() ) st = strOf( c, "status" );
        // not_earned / indeterminate must never become pass
        cr.status = normalizeStatus( st );
        if ( st == "earned" || st == "full" ) {
          cr.status = "pass";
        }
        cr.statusZh = statusZh( cr.status );
        cr.countsAsPass = countsAsPass( cr.status );
        if ( cr.status == "indeterminate" ) cr.countsAsPass = false;
        // Reasons: slugs only — do not copy expected golden values.
        if ( c.isMember( "reasons" ) && c["reasons"].isArray() ) {
          for ( const auto &r : c["reasons"] ) {
            if ( r.isString() ) {
              if ( !cr.reasonZh.empty() ) cr.reasonZh += "; ";
              cr.reasonZh += r.asString();
            } else if ( r.isObject() ) {
              const std::string slug = strOf( r, "slug" );
              if ( !slug.empty() ) {
                if ( !cr.reasonZh.empty() ) cr.reasonZh += "; ";
                cr.reasonZh += slug;
              }
            }
          }
        }
        if ( c.isMember( "evidenceIds" ) && c["evidenceIds"].isArray() ) {
          for ( const auto &e : c["evidenceIds"] )
            if ( e.isString() ) cr.evidenceIds.push_back( e.asString() );
        } else if ( c.isMember( "evidence_ids" ) && c["evidence_ids"].isArray() ) {
          for ( const auto &e : c["evidence_ids"] )
            if ( e.isString() ) cr.evidenceIds.push_back( e.asString() );
        }
        p.rows.push_back( cr );
        agg.push_back( cr.status );
        ++i;
      }
    }
    p.scienceValidationSummaryZh = std::string( "科学/过程评分: " ) + grow.statusZh;

    // sicnu.lab.grade/1 canonical body (OutputVerifier::LabGradeResult —
    // the same document `lab --grade` records): project per-assertion rows
    // from evidence[]. Deduction messages are the only free text copied;
    // observed/expected payloads are engine-owned goldens and never leak.
    if ( gNorm == "indeterminate" ) {
      const std::string err = strOf( graderReport, "error" );
      if ( !err.empty() ) {
        grow.reasonZh = err;
        p.issuesZh.push_back( "评分不可用: " + err );
      }
    }
    const auto &evidence = graderReport["evidence"];
    if ( evidence.isArray() ) {
      std::map<std::string, std::string> deductionMsg;
      const auto &deductions = graderReport["deductions"];
      if ( deductions.isArray() ) {
        for ( const auto &d : deductions ) {
          if ( !d.isObject() ) continue;
          const std::string id = strOf( d, "assertion_id" );
          const std::string msg = strOf( d, "message" );
          if ( !id.empty() && !msg.empty() ) deductionMsg[id] = msg;
        }
      }
      int i = 0;
      for ( const auto &e : evidence ) {
        if ( !e.isObject() ) continue;
        FeedbackCheckRow cr;
        cr.id = strOf( e, "assertion_id" );
        if ( cr.id.empty() ) cr.id = "grader.assertion." + std::to_string( i );
        cr.layer = "grader";
        // passed=true is the engine's own pass; anything else is a fail —
        // never promoted.
        cr.status = e.isMember( "passed" ) && e["passed"].isBool() && e["passed"].asBool()
                      ? "pass"
                      : "fail";
        cr.statusZh = statusZh( cr.status );
        const auto msg = deductionMsg.find( cr.id );
        if ( msg != deductionMsg.end() && cr.status != "pass" ) cr.reasonZh = msg->second;
        cr.countsAsPass = countsAsPass( cr.status );
        p.rows.push_back( cr );
        agg.push_back( cr.status );
        ++i;
      }
    }
  } else {
    p.scienceValidationSummaryZh = "科学/过程评分: 未提供（不确定）";
    agg.push_back( "indeterminate" );
  }

  // Final invariant sweep
  for ( auto &row : p.rows ) {
    if ( row.status == "indeterminate" || row.status == "not_earned" ) row.countsAsPass = false;
  }

  p.overallStatus = aggregate( agg );
  p.overallStatusZh = statusZh( p.overallStatus );
  p.overallCountsAsPass = countsAsPass( p.overallStatus );
  // Empty inputs → indeterminate, never pass
  if ( !verifierReport.isObject() && !graderReport.isObject() ) {
    p.overallStatus = "indeterminate";
    p.overallStatusZh = statusZh( p.overallStatus );
    p.overallCountsAsPass = false;
    p.issuesZh.push_back( "无验证/评分报告 → 不确定（非通过）" );
  }
  p.ok = true;
  return p;
}

LabFeedbackProjection LabFeedbackProjection::fromJson( const Json::Value &doc )
{
  LabFeedbackProjection p; // ok=false until proven parseable
  auto refuseIndeterminate = [&p]( const std::string &issue )
  {
    p.issuesZh.push_back( issue );
    p.overallStatus = "indeterminate";
    p.overallStatusZh = statusZh( p.overallStatus );
    p.overallCountsAsPass = false;
    return p;
  };
  if ( !doc.isObject() || !doc.isMember( "schema" ) || !doc["schema"].isString()
       || doc["schema"].asString() != p.schema )
  {
    return refuseIndeterminate( "反馈摘要 schema 不匹配（按不确定处理）" );
  }
  p.labId = strOf( doc, "lab_id" );
  p.overallStatus = strOf( doc, "overall_status" );
  const std::string norm = normalizeStatus( p.overallStatus );
  if ( norm != p.overallStatus || ( p.overallStatus != "pass" && p.overallStatus != "fail"
                                    && p.overallStatus != "indeterminate" ) )
  {
    p.issuesZh.push_back( "反馈摘要 overall_status 非法（按不确定处理）" );
    p.overallStatus = "indeterminate";
  }
  p.overallStatusZh = statusZh( p.overallStatus );
  if ( doc.isMember( "overall_counts_as_pass" ) && doc["overall_counts_as_pass"].isBool() )
    p.overallCountsAsPass = doc["overall_counts_as_pass"].asBool();
  else
    p.overallCountsAsPass = countsAsPass( p.overallStatus );
  // INVARIANT sweep on restore: an indeterminate summary can never pass.
  if ( p.overallStatus != "pass" ) p.overallCountsAsPass = false;

  p.techValidationSummaryZh = strOf( doc, "tech_validation_summary_zh" );
  p.scienceValidationSummaryZh = strOf( doc, "science_validation_summary_zh" );
  p.capsuleExportRef = strOf( doc, "capsule_export_ref" );
  if ( doc.isMember( "grader_score" ) && doc["grader_score"].isObject() )
    p.graderScore = doc["grader_score"];
  if ( doc.isMember( "issues_zh" ) && doc["issues_zh"].isArray() )
    for ( const auto &i : doc["issues_zh"] )
      if ( i.isString() ) p.issuesZh.push_back( i.asString() );

  const auto &rows = doc["rows"];
  if ( rows.isArray() )
  {
    for ( const auto &r : rows )
    {
      FeedbackCheckRow cr;
      cr.id = strOf( r, "id" );
      cr.layer = strOf( r, "layer" );
      cr.status = strOf( r, "status" );
      cr.statusZh = strOf( r, "status_zh" );
      cr.reasonZh = strOf( r, "reason_zh" );
      cr.countsAsPass = r.isMember( "counts_as_pass" ) && r["counts_as_pass"].isBool()
                          && r["counts_as_pass"].asBool();
      if ( cr.status == "indeterminate" || cr.status.empty() ) cr.countsAsPass = false;
      if ( r.isMember( "evidence_ids" ) && r["evidence_ids"].isArray() )
        for ( const auto &e : r["evidence_ids"] )
          if ( e.isString() ) cr.evidenceIds.push_back( e.asString() );
      p.rows.push_back( cr );
    }
  }
  // A restored summary keeps its recorded verdict — including an engine
  // refusal, which must still not render as pass.
  if ( p.overallStatus.empty() )
  {
    p.overallStatus = "indeterminate";
    p.overallStatusZh = statusZh( p.overallStatus );
    p.overallCountsAsPass = false;
  }
  p.ok = true;
  return p;
}

Json::Value LabFeedbackProjection::toJson() const
{
  Json::Value root( Json::objectValue );
  root["schema"] = schema;
  root["ok"] = ok;
  root["lab_id"] = labId;
  root["overall_status"] = overallStatus;
  root["overall_status_zh"] = overallStatusZh;
  root["overall_counts_as_pass"] = overallCountsAsPass;
  root["tech_validation_summary_zh"] = techValidationSummaryZh;
  root["science_validation_summary_zh"] = scienceValidationSummaryZh;
  root["capsule_export_ref"] = capsuleExportRef;
  root["grader_score"] = graderScore;
  Json::Value issues( Json::arrayValue );
  for ( const auto &i : issuesZh ) issues.append( i );
  root["issues_zh"] = issues;
  Json::Value arr( Json::arrayValue );
  for ( const auto &r : rows ) {
    Json::Value o( Json::objectValue );
    o["id"] = r.id;
    o["layer"] = r.layer;
    o["status"] = r.status;
    o["status_zh"] = r.statusZh;
    o["reason_zh"] = r.reasonZh;
    o["counts_as_pass"] = r.countsAsPass;
    Json::Value ev( Json::arrayValue );
    for ( const auto &e : r.evidenceIds ) ev.append( e );
    o["evidence_ids"] = ev;
    arr.append( o );
  }
  root["rows"] = arr;
  return root;
}

} // namespace sicnu::teaching
