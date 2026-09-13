// src/agent/harness/lab_copilot.h
#pragma once

//
// D9: the lab (teaching) copilot orchestration.
//
// One entry point, labAsk(), turns a student/teacher request into the
// structured teaching answer. The hard rule lives one layer down —
// harness_actions withholds artifact-producing actions from students — but
// this module is where intents, roles, grounding, and answers meet:
//
//   * role comes from the request's session field (default student), NEVER
//     from message content;
//   * lab_execute / lab_grade_request are teacher surfaces: a student gets a
//     typed TEACHING_REFUSAL envelope, not a soft apology;
//   * tutoring answers carry a diagnosis (symptom + likely cause + exactly
//     one verification action), a hint anchored to the current LabSpec step,
//     or one concept — Chinese-first, glossary terms verbatim;
//   * every suggested action resolves through the role-gated twin
//     (resolvedSuggestedActionForRole), so no artifact surface can leak into
//     a student answer even if a producer above misclassifies.
//
// Envelope shape (same style as the harness error contract):
//   refusal: { success: false, error: {code: "TEACHING_REFUSAL", ...},
//              refusal: {refused, intent, role, reason_zh, alternative_zh} }
//   answer:  { success: true, result: {intent, role, answer_zh, ...} }
//

#include <json/json.h>
#include <string>

namespace sicnu::agent::harness {

/// The teaching refusal envelope (typed, structured, non-retryable).
/// `intent` is what was attempted; `role` the normalized session role.
Json::Value teachingRefusalEnvelope( const std::string &intent, const std::string &role );

/// Full teaching answer for one lab request (the harness:lab_ask contract).
Json::Value labAsk( const Json::Value &input );

/// Teacher reference surface (reference solutions, grade citation).
/// Students receive a teaching refusal instead.
Json::Value labReference( const Json::Value &input );

/// True when `input` carries a session role that may use the teacher
/// surfaces. Kept next to the gate so the policy reads in one place.
bool labRoleMayUseTeacherSurfaces( const std::string &role );

} // namespace sicnu::agent::harness
