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

/// A file the discovery heuristics refused to treat as a submission
/// (reserved tool-output name: grades* prefix, or the batch/summary/
/// feedback substrings). Refusals are deliberate — re-grading must not
/// pick up the console's own outputs — but they are ACCOUNTED, never a
/// silent vanish: every refusal reaches the published report (#1466).
struct DiscoverySkip
{
    QString studentId;   ///< owning one-level student dir; empty for root-level files
    QString path;        ///< absolute path of the refused file
    QString reason;      ///< stable human-readable refusal reason
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
    // Real-grader traceability (cliGradeCallable): digest of the
    // sicnu.lab.grade/1 transcript body and the first deduction id. Empty for
    // non-grader rows; additive fields, sorted-key serialization unchanged.
    QString graderDigest;
    QString topDeduction;
    /// Mandatory when status == "unavailable" and the cause is grader-side
    /// (vocabulary of src/cli/lab_batch_runner.h); never set for
    /// missing-evidence rows.
    QString unavailableReason;
    /// True when the row was adopted from a durable checkpoint rather than
    /// graded by the publishing run (R18-E D3). Orchestrator-level state:
    /// deliberately NOT serialized into the row document, so a resumed run
    /// publishes byte-identical rows to an uninterrupted one; published
    /// visibility is the report-level `resumed` counter.
    bool resumed = false;

    QJsonObject toJson() const;
    /// Rebuilds a row from toJson()'s output (checkpoint round-trip).
    static BatchRowResult fromJson( const QJsonObject &o );
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
    /// Non-empty ⇒ durable partial results: after EVERY row that went through
    /// the grader callable, the completed rows are atomically rewritten to
    /// this file (temp + rename). A later run over the same config digest
    /// adopts those rows (counted in BatchAssessmentReport::resumed) and
    /// re-grades only the interrupted remainder. Cancelled items are never
    /// checkpointed, so a restart re-grades them instead of disguising them
    /// as finished.
    QString checkpointPath;
};

struct BatchAssessmentReport
{
    QString schema = QString::fromLatin1( kBatchOrchestrationSchema );
    QString labId;
    int total = 0;   ///< submissions DISCOVERED (processed + truncated)
    int graded = 0;
    int failed = 0;
    int corrupted = 0;
    int cancelled = 0;
    int missingEvidence = 0;
    /// Rows whose grade could not be produced at all (grader missing/timeout),
    /// counted after the run from final row status. Distinct from
    /// missingEvidence (evidence never arrived) — neither is a silent zero.
    int unavailable = 0;
    bool cancelledEarly = false;
    /// Discovered submissions dropped by the maxSubmissions cap — the cap is
    /// typed in the report, never a silent truncation.
    int truncated = 0;
    /// Rows adopted from a matching checkpoint instead of re-graded.
    int resumed = 0;
    /// Files discovery refused (reserved tool-output names) — accounted so
    /// a submission named e.g. zhang_feedback.json is an explicit refusal
    /// in the report/CSV instead of vanishing without a trace (#1466).
    /// These files are NOT rows and never enter the checkpoint.
    QVector<DiscoverySkip> skippedFiles;
    QString rubricVersion;
    QString labVersion;
    QString softwareVersion;
    QVector<BatchRowResult> rows;

    QJsonObject toJson() const;
};

/// Discover submissions under a class directory (non-recursive files + one-level student dirs).
QVector<SubmissionItem> discoverSubmissions( const QString &submissionsDir );

/// Same discovery, accounting every refused file into @p skipped (owned by
/// the caller; pass nullptr for the legacy skip-silent behavior — the
/// batch orchestrator always passes a real vector so the report can carry
/// the refusals).
QVector<SubmissionItem> discoverSubmissions( const QString &submissionsDir,
                                             QVector<DiscoverySkip> *skipped );

SubmissionKind classifySubmission( const QString &path );

/// Local batch grade using an injected grader callable.
/// The callable must never throw across the boundary; return typed status instead.
/// (A throwing callable is isolated into one typed error row.)
///
/// Runs on a bounded worker pool of min( maxConcurrency clamped to [1,16],
/// pending items ) threads; results are assembled in the deterministic
/// discovery order regardless of completion order. Cancellation is checked
/// before each item starts: completed work is kept, never-started items get
/// typed "cancelled" rows — they are never disguised as failures.
using GradeCallable = std::function<BatchRowResult( const SubmissionItem &item )>;

/// Progress probe: invoked from worker threads after each row that went
/// through the callable — @p done counts graded/failed rows, @p total is the
/// number of processed submissions (adopted checkpoint rows are NOT
/// re-counted). Must be thread-safe and cheap; UI callers marshal to the
/// GUI thread themselves.
using BatchProgressFn = std::function<void( int done, int total )>;

BatchAssessmentReport runBatchAssessment( const BatchAssessmentConfig &cfg, const GradeCallable &grade,
                                          std::atomic<bool> *cancelFlag = nullptr,
                                          const BatchProgressFn &progress = {} );

/// Atomically publish JSON + CSV (temp + rename). Returns false on I/O failure.
bool publishBatchOutputsAtomic( const BatchAssessmentReport &report, const QString &outPrefix );

/// Deterministic regrade traceability block.
QJsonObject regradeTraceability( const BatchAssessmentReport &report );

} // namespace sicnu::teaching_admin
