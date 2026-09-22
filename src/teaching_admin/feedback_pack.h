// feedback_pack.h — per-student feedback pack (no cross-student leak).
#pragma once

#include "admin_types.h"
#include "batch_assessment.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace sicnu::teaching_admin {

struct FeedbackPackInput
{
    BatchRowResult row;
    QJsonObject dimensionScores;     ///< optional rubric dimension breakdown
    QJsonArray verifierFindings;     ///< typed findings
    QJsonArray missingEvidence;      ///< list of missing evidence ids
    QJsonObject firstDivergence;     ///< optional adapter projection
    QJsonObject reproducibility;     ///< capsule/run digests etc.
    QString teacherCommentPlaceholder;
};

QJsonObject buildFeedbackPack( const FeedbackPackInput &in );

/// Ensure feedback pack contains only the given student_id (no roster leak).
ValidationResult assertNoCrossStudentLeak( const QJsonObject &feedback, const QString &studentId );

} // namespace sicnu::teaching_admin
