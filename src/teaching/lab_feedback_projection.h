// LabFeedbackProjection — verifier + grader student surface.
// INVARIANT: indeterminate MUST NOT render as pass.
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::teaching {

struct FeedbackCheckRow {
  std::string id;
  std::string layer; // verifier | grader
  std::string status; // pass | fail | indeterminate | not_earned | ...
  std::string statusZh;
  std::string reasonZh;
  std::vector<std::string> evidenceIds;
  bool countsAsPass = false; // NEVER true for indeterminate
};

struct LabFeedbackProjection {
  std::string schema = "sicnu.teaching.lab_feedback/1";
  bool ok = false;
  std::string labId;
  /// Aggregate: fail > indeterminate > pass; empty → indeterminate (fail-closed).
  std::string overallStatus; // pass | fail | indeterminate
  std::string overallStatusZh;
  bool overallCountsAsPass = false;
  std::string techValidationSummaryZh;    // verifier lens
  std::string scienceValidationSummaryZh; // grader / claims lens
  std::vector<FeedbackCheckRow> rows;
  std::vector<std::string> issuesZh;
  std::string capsuleExportRef; // pointer only
  /// {score,passing_score,capped_by_blocking} for a sicnu.lab.grade/1 body,
  /// {earned,max} for rubric reports — scale only, never golden answers.
  Json::Value graderScore;

  static LabFeedbackProjection fromReports( const std::string &labId,
                                            const Json::Value &verifierReport,
                                            const Json::Value &graderReport,
                                            const std::string &capsuleRef = {} );

  /// Re-materialize a persisted toJson() document (restart restore).
  /// Fail-closed: a wrong-schema document is refused outright (ok=false);
  /// a schema-valid document with an illegal overall_status or wrong types
  /// keeps ok=true but downgrades to an indeterminate overall that can
  /// never count as pass — a restored summary never fabricates a pass.
  static LabFeedbackProjection fromJson( const Json::Value &doc );

  Json::Value toJson() const;
};

} // namespace sicnu::teaching
