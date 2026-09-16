/***************************************************************************
 * src/cli/lab_batch_runner.h — D7 batch grading engine, v2 (batch classroom)
 *
 * Streams a submissions directory through the OutputVerifier::gradeArtifact
 * teaching seam (ADR 0150), one submission at a time, appending a CSV row
 * (UTF-8 with BOM) and flushing it before the next submission is graded.
 * Artifact BYTES are never held: memory stays bounded by the largest single
 * artifact, never by class size. (v2 summary rows are tiny text — student id,
 * verdict, score, content digest — the only O(class-size) state, so the
 * JSON/HTML summaries can be emitted without re-reading artifacts.)
 * A throwing grade call is isolated into an `error` row; the run continues.
 *
 * v2 additions (teaching-lab-platform-11), all optional and backward
 * compatible:
 *   * identity     — every submission carries a streaming sha256; identical
 *                    content from two files is graded once per file but
 *                    flagged duplicate_of (the teacher judges, the machine
 *                    never invents a policy);
 *   * roster       — optional roster CSV (student_id,display_name) marks
 *                    unknown submitters and missing students in the summary;
 *   * caps         — --max-submissions stops before runaway input; the CSV
 *                    keeps every row written so far;
 *   * cancellation — an injected cancelled() probe between submissions
 *                    (graded runs stay resumable by re-running: the CSV is
 *                    rewritten deterministically);
 *   * summaries    — deterministic JSON (sicnu.lab.batch-summary/1) and HTML
 *                    renderings; atomic (tmp+rename) like every export.
 *
 * This file is D7-owned; cli_lab_commands.cpp only parses flags and
 * delegates here.
 ***************************************************************************/
#pragma once

#include "agent/output_verifier.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace sicnu::cli {

struct LabBatchRow
{
    QString studentId;
    QString labId;
    double score = -1.0;      ///< < 0 when no score (error / unverifiable)
    QString verdict;          ///< "pass" | "fail" | "unverifiable" | "error"
    QString topDeduction;
    QString artifactPath;
    QString sha256;           ///< lowercase hex, empty when hashing failed
    qint64 bytes = -1;
    QString duplicateOf;      ///< student id of the first identical content
    bool rosterMatch = true;  ///< false when a roster exists and lacks this id
};

struct LabBatchSummary
{
    int total = 0;       ///< submissions discovered (CSV rows written)
    int graded = 0;      ///< graded == true (verdict pass/fail)
    int isolated = 0;    ///< grade callable threw; row carries verdict "error"
    bool usageError = false; ///< submissions directory missing/unreadable
    QString usageMessage;    ///< typed reason when usageError
    // v2
    int duplicates = 0;      ///< rows flagged duplicate_of
    int unknownRoster = 0;   ///< submitters absent from the (optional) roster
    int missingRoster = 0;   ///< roster ids without a submission
    bool cappedByMaxSubmissions = false;
    bool cancelled = false;  ///< stopped by the cancel probe mid-run
    QVector<LabBatchRow> rows; ///< O(class) tiny text rows for the summaries

    /// Roster-ordered student ids with no submission (sorted).
    QStringList missingRosterIds;
};

/// Optional roster: UTF-8 (BOM tolerated) CSV with header
/// `student_id,display_name`. Blank lines and #comments are skipped.
class LabRoster
{
  public:
    /// Loads and validates; usage-fails with a typed message when the file
    /// is unreadable, unparseable, or has duplicate student ids.
    static bool load( const QString &path, LabRoster *roster, QString *error );

    bool contains( const QString &studentId ) const;
    QStringList ids() const;   ///< sorted
    QString displayName( const QString &studentId ) const;

  private:
    QMap<QString, QString> m_entries; // student_id -> display_name
};

struct LabBatchOptions
{
    QString rosterPath;        ///< empty = no roster cross-check
    QString jsonPath;          ///< empty = no JSON summary
    QString htmlPath;          ///< empty = no HTML summary
    int maxSubmissions = -1;   ///< < 0 = unlimited
    std::function<bool()> cancelled; ///< probed between submissions
};

/// Injected so tests (and future callers) can drive the engine without GDAL:
/// the production callable is `verifier.gradeArtifact(lab, path, options)`.
using LabGradeFn = std::function< sicnu::agent::OutputVerifier::LabGradeResult(
  const QString &artifactPath )>;

struct LabBatchRunner
{
    /// CSV schema (UTF-8 BOM): student_id,lab_id,score,verdict,top_deduction,
    /// artifact_path. student_id = submission file stem; discovery order is
    /// sorted by filename; hidden files, subdirectories and the CSV target
    /// itself are skipped.
    static LabBatchSummary run( const QString &submissionsDir, const QString &labId,
                                const QString &csvPath, const LabGradeFn &grade,
                                const LabBatchOptions &options = LabBatchOptions() );

    /// Deterministic summary document body (no wall-clock values). Rows are
    /// ordered by student_id. Emitters embed it as
    /// {"schema":"sicnu.lab.batch-summary/1", "lab_id", ..., "rows"}.
    static QJsonObject summaryBodyJson( const QString &labId, const QString &submissionsDir,
                                        const LabBatchSummary &summary );
};

/// Exit-code contract for `lab --batch` (exprs ExitCode values):
///   Ok               — every discovered submission graded (even all "fail")
///   GenericError     — run completed but isolated >= 1 error row, was
///                      cancelled, or hit the submission cap
///   ValidationFailure — usage problems (missing dirs, bad roster/CSV target)
int batchExitCodeFor( const LabBatchSummary &summary );

} // namespace sicnu::cli
