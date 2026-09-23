// grader_render.h — report renderers over ONE GradeReport (ADR 0174: the
// same machine-readable report feeds every audience). Pure JSON views, no
// I/O, deterministic.
//
//   renderStudentFeedback   — what the student may see. NEVER carries rubric
//                             internals: no teacher hints, no requiredFacts
//                             values, no misconception patterns, no accepted
//                             alternatives. The golden answers stay with
//                             the teacher. Explanations in the report are
//                             student-safe by construction, so they pass
//                             through verbatim.
//   renderTeacherDiagnostics — the teacher's triage view: the full reason
//                             chain, cited evidence, teacher hints joined
//                             from the rubric, constraint/stage/pathway
//                             detail and the indeterminate-item list.
//   renderMachineSummary     — compact verdict document for programmatic
//                             consumers: score/verdict/digests, blocked
//                             constraints, matched pathway. No per-criterion
//                             detail.
//
// View schemas (informational, additive):
//   sicnu.grader.student-feedback/1
//   sicnu.grader.teacher-diagnostics/1
//   sicnu.grader.machine-summary/1
#pragma once

#include "grader_types.h"

#include <json/json.h>

namespace sicnu::grader {

/// Student-facing feedback. @p rubric is OPTIONAL: when given, criterion
/// titles and student hints are joined in (student members only — the
/// renderer structurally cannot emit teacher members).
/// Note: views trust the report they are given. Reports parsed from external
/// JSON should pass verifyDigest() before rendering (the tamper gate is the
/// caller's contract, as for every GradeReport consumer).
Json::Value renderStudentFeedback( const GradeReport &report, const GradingRubric *rubric );

/// Teacher-facing diagnostics. The rubric is required (hints are joined).
Json::Value renderTeacherDiagnostics( const GradeReport &report, const GradingRubric &rubric );

/// Compact machine summary (no rubric needed).
Json::Value renderMachineSummary( const GradeReport &report );

} // namespace sicnu::grader
