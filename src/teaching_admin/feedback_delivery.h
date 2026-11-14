// feedback_delivery.h — guarded LOCAL feedback delivery (R30-T07 WP11).
//
// The teacher console built real guarded packs and a class summary, then
// only APPENDED them to a log. This module writes them as portable local
// files with an explicit destination, conflict policy and partial status.
// It NEVER uploads, emails or messages anyone, and NEVER invents a finding
// or a score: every byte comes from the real BatchAssessmentReport / the
// guard-passed FeedbackPackEmission.
#pragma once

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/feedback_pack.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::teaching_admin
{

struct FeedbackDeliveryRequest
{
    QString destinationDir;      ///< explicit local destination (required)
    QString courseId;
    QString labId;
    QString batchReportPath;     ///< real batch report the evidence came from
    QString batchConfigDigest;   ///< release preflight digest, when available
    bool overwriteExisting = false; ///< default: report a conflict, never overwrite
};

struct FeedbackDeliveryResult
{
    bool ok = false;
    bool partial = false;
    QString code;
    QString message;
    QString manifestPath;
    QStringList writtenPacks;    ///< absolute local paths
    QStringList conflicts;       ///< skipped because a file already exists
    QStringList writeFailures;   ///< typed per-student failures
    QStringList blockedStudents; ///< guard refusals, never shipped
    QStringList refusals;
    QString summaryPath;
    QString batchReportDigest;   ///< sha256 of the real report bytes
    int guardedIn = 0;

    QJsonObject toJson() const;
};

/// Atomic, bounded delivery. Uses the platform sidecar write authority for
/// every file (temp → fsync → rename → read-back verify). A failure on one
/// student leaves a truthful PARTIAL manifest; it is never logged as success.
FeedbackDeliveryResult deliverGuardedFeedback(
    const QVector<QJsonObject> &guardedPacks,
    const QJsonObject &classSummary,
    const QStringList &blockedStudents,
    const QStringList &refusals,
    const FeedbackDeliveryRequest &request );

} // namespace sicnu::teaching_admin
