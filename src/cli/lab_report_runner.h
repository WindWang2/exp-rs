/***************************************************************************
 * src/cli/lab_report_runner.h — headless `sicnu.labreport.v1` export.
 *
 * The D5 LabReportBuilder has always been the report authority, but it was
 * reachable only from the GUI. This runner is a thin CLI shell over the SAME
 * builder + writers: opens the ExperimentStore at --experiment-db, projects
 * the recorded runs/trail/lineage/replay state, and — when a grade
 * transcript from `lab --grade --out` is supplied — embeds it as the
 * RECORDED grade variant (gradingRef = the transcript's digest; the inline
 * copy carries the transcript's canonical body). The builder's projection
 * contract is untouched: a report is recorded truth, never a recomputation.
 *
 * Deterministic: the generated_at header comes from --generated-at-utc when
 * given (tests fix it), else now; identical inputs + identical header give
 * byte-identical files. All writes go through the D5 writers (validated,
 * atomic per file).
 ***************************************************************************/
#pragma once

#include <QString>

namespace sicnu::cli {

struct LabReportOptions
{
    QString experimentDb;        ///< ExperimentStore path (required)
    QString experimentId;        ///< experiment id in the store (required)
    QString labName;             ///< header metadata (fallback: store value)
    QString objective;           ///< header metadata (fallback: store value)
    QString student;             ///< header metadata (empty stays empty)
    QString session;             ///< header metadata (empty stays empty)
    QString runId;               ///< primary run pin (empty = deterministic pick)
    QString gradeTranscriptPath; ///< `lab --grade --out` document (optional)
    /// Output base path WITHOUT extension: writes <base>.json/.md/.html via
    /// writeLabReportFiles (all three — the teacher picks a rendering).
    QString outBase;
    /// Injected generation timestamp (tests); empty = now (header only).
    QString generatedAtUtc;
};

/// Runs the export. Returns an exprs exit code (Ok / ValidationFailure for
/// usage problems) and fills @p errorOut with the typed reason on failure.
int runLabReport( const LabReportOptions &options, std::string *errorOut );

} // namespace sicnu::cli
