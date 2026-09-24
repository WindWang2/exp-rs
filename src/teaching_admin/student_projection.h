// student_projection.h — student-safe projection of an authored LabSpec.
//
// Same authoring truth as the teacher projections (LabSpec in, no second
// copy): teacher-only material — grading_ref, expected_results, and step
// parameter VALUES (they are the solution, cf. LabSpecCatalog::stepDoc and
// lab_step_timeline masking) — never enters the student view. The leak
// oracle assertNoAnswerLeak cross-checks the projection against the teacher
// spec it came from.
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QString>

namespace sicnu::teaching_admin {

/// Project a LabSpec (sicnu.labspec.v1 or D2 lab.json) to the student view:
/// pedagogical fields + steps with param values masked to "***" (names stay,
/// mirroring the cockpit's paramsDisplay convention). Deterministic
/// (sorted keys, stable arrays).
QJsonObject projectStudentLabView( const QJsonObject &labSpec );

/// Adversarial oracle: given a student view and the teacher spec it must
/// have been derived from, verify no teacher-only material leaked. An empty
/// or absent field on the teacher side is not a leak; a match is a typed
/// error (answer_leak / teacher_only_field).
ValidationResult assertNoAnswerLeak( const QJsonObject &studentView, const QJsonObject &teacherSpec );

} // namespace sicnu::teaching_admin
