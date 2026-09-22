// batch_assessment.h — Batch Assessment orchestration (import → grade → export).
// One bad submission must not kill the batch; missing evidence ≠ silent zero.
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

namespace sicnu::teaching_admin {

enum class SubmissionKind
{
    Unknown,
    ArtifactFile,
    LabReport,
    Capsule,
    Bundle,
};

struct SubmissionItem
{
    QString studentId;
    QString path;
    SubmissionKind kind = SubmissionKind::Unknown;
    QString sha256;
    qint64 bytes = -1;
};

struct BatchRowResult
{
    QString studentId;
    QString labId;
    QString status;   ///< pass|fail|error|timeout|crash|unavailable|cancelled|corrupted
    double score = -1.0;
    QString verdict;
    QString message;
    QString artifactPath;
    QString rubricVersion;
    QString labVersion;
    QString softwareVersion;
    bool missingEvidence = false;

    QJsonObject toJson() const;
};

struct BatchAssessmentConfig
{
    QString labId;
    QString submissionsDir;
    QString outDir;
    QString rubricVersion;
    QString labVersion;
    QString softwareVersion;
    int maxConcurrency = 2;
    int maxSubmissions = 10000;
};

struct BatchAssessmentReport
{
    QString schema = QString::fromLatin1( kBatchOrchestrationSchema );
    QString labId;
    int total = 0;
    int graded = 0;
    int failed = 0;
    int corrupted = 0;
    int cancelled = 0;
    int missingEvidence = 0;
    bool cancelledEarly = false;
    QString rubricVersion;
    QString labVersion;
    QString softwareVersion;
    QVector<BatchRowResult> rows;

    QJsonObject toJson() const;
};

/// Discover submissions under a class directory (non-recursive files + one-level student dirs).
QVector<SubmissionItem> discoverSubmissions( const QString &submissionsDir );

SubmissionKind classifySubmission( const QString &path );

/// Local in-process batch grade using an injected grader callable.
/// The callable must never throw across the boundary; return typed status instead.
using GradeCallable = std::function<BatchRowResult( const SubmissionItem &item )>;

BatchAssessmentReport runBatchAssessment( const BatchAssessmentConfig &cfg, const GradeCallable &grade,
                                          std::atomic<bool> *cancelFlag = nullptr );

/// Atomically publish JSON + CSV (temp + rename). Returns false on I/O failure.
bool publishBatchOutputsAtomic( const BatchAssessmentReport &report, const QString &outPrefix );

/// Deterministic regrade traceability block.
QJsonObject regradeTraceability( const BatchAssessmentReport &report );

} // namespace sicnu::teaching_admin
