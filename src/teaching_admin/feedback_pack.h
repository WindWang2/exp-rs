// feedback_pack.h — per-student feedback pack (no cross-student leak).
#pragma once

#include "admin_types.h"
#include "batch_assessment.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

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
/// Legacy two-argument form: only the owner check plus the student_-prefix
/// token scan — kept for existing callers/tests.
ValidationResult assertNoCrossStudentLeak( const QJsonObject &feedback, const QString &studentId );

/// #1689 cohort-aware form: in addition to the legacy scan, every OTHER
/// student id of the batch is matched against the pack blob with word
/// boundaries. Ids in the wild are plain numeric student numbers
/// (2023110101) or Chinese names, which the prefix scan cannot see; here
/// the roster itself defines what counts as a foreign id, so any id shape
/// is covered. Substring adjacency does not trip it (2023002 does not match
/// inside 20230021), and the owner's own id is allowed anywhere in its own
/// pack.
ValidationResult assertNoCrossStudentLeak( const QJsonObject &feedback, const QString &studentId,
                                           const QStringList &cohortStudentIds );

/// #1689 production gate: build every pack AND run the cohort-aware guard
/// BEFORE anything is emitted — a pack that fails is withheld (never
/// printed, exported or distributed) and its student is accounted in
/// blockedStudents, so a suspected leak is a visible refusal, not a silent
/// drop. The teacher console routes its feedback output through this.
struct FeedbackPackEmission
{
    QVector<QJsonObject> packs;    ///< guard-passed packs, input order
    QStringList blockedStudents;   ///< students whose pack was refused
    /// One line per refusal: "<studentId>: <first issue message>".
    QStringList refusals;
};
FeedbackPackEmission buildGuardedFeedbackPacks( const QVector<FeedbackPackInput> &inputs,
                                                const QStringList &cohortStudentIds );

} // namespace sicnu::teaching_admin
