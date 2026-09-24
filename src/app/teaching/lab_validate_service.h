// lab_validate_service.h — real verifier/grader wiring for the lab cockpit.
//
// The cockpit's 验证与评分 button must consume the SAME engines the
// classroom CLI uses (OutputVerifier::verify + gradeForTeaching, the wrapper
// of `lab --grade`), projected through LabFeedbackProjection. Nothing here
// recomputes a verdict in the UI layer: engine unavailable / rules missing /
// artifact unreadable all project honestly as indeterminate or fail, never
// as pass, and no ref is fabricated.
#pragma once

#include "teaching/lab_feedback_projection.h"

#include <json/json.h>

#include <string>

namespace sicnu::app::teaching {

struct LabValidateInput
{
  std::string labId;
  std::string artifactPath; // student-produced output to validate (may be empty)
  std::string rulesPath;    // resolved grading rules path (may be empty)
};

/// Resolves a lab document's `grading_rules` repo-relative reference against
/// the repo data root ("<repo>/data"). Absolute refs pass through verbatim;
/// a lab without a grading_rules member yields an empty path (the grader
/// then falls back to its own lab-id resolution and may honestly refuse).
std::string resolveLabRulesPath( const Json::Value &labDoc,
                                 const std::string &repoDataRoot );

/// Runs the real verifier + grader providers for one artifact and projects
/// both lenses. An empty artifactPath short-circuits to the honest
/// indeterminate projection (issues note what was NOT executed).
sicnu::teaching::LabFeedbackProjection projectValidation(
  const LabValidateInput &input );

} // namespace sicnu::app::teaching
