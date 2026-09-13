/***************************************************************************
 * src/cli/lab_batch_runner.h — D7 batch grading engine
 *
 * Streams a submissions directory through the OutputVerifier::gradeArtifact
 * teaching seam (ADR 0146), one submission at a time, appending a CSV row
 * (UTF-8 with BOM) and flushing it before the next submission is graded —
 * memory stays bounded by the largest single artifact, never by class size.
 * A throwing grade call is isolated into an `error` row; the run continues.
 *
 * This file is D7-owned; cli_lab_commands.cpp only parses `--batch`/`--csv`
 * and delegates here.
 ***************************************************************************/
#pragma once

#include "agent/output_verifier.h"

#include <QString>
#include <functional>

namespace sicnu::cli {

struct LabBatchSummary
{
    int total = 0;       ///< submissions discovered (CSV rows written)
    int graded = 0;      ///< graded == true (verdict pass/fail)
    int isolated = 0;    ///< grade callable threw; row carries verdict "error"
    bool usageError = false; ///< submissions directory missing/unreadable
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
                                const QString &csvPath, const LabGradeFn &grade );
};

/// Exit-code contract for `lab --batch` (exprs ExitCode values):
///   Ok               — every discovered submission graded (even all "fail")
///   GenericError     — run completed but isolated >= 1 error row
///   ValidationFailure — usage problems (missing/unreadable submissions dir)
int batchExitCodeFor( const LabBatchSummary &summary );

} // namespace sicnu::cli
