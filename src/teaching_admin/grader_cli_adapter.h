// grader_cli_adapter.h — teacher-side grading over the REAL grader authority.
//
// The only grading authority in this repo is OutputVerifier::gradeArtifact()
// (src/agent, ADR 0150); the `sicnu_geo_rs_cli lab --grade` subprocess is its
// process-isolated shell with a typed exit contract (0 pass / 1 fail /
// 2 usage / 3 unverifiable) and a {schema, digest, generated_utc, report}
// transcript — the same invocation scripts/run_classroom_batch.py drives.
// This adapter shells out to that CLI; it computes no scores itself and
// fabricates nothing: when the grader cannot run, the row is typed
// "unavailable" with a mandatory reason (vocabulary of
// src/cli/lab_batch_runner.h), never a silent zero or a pass.
#pragma once

#include "admin_types.h"
#include "batch_assessment.h"

#include <QJsonObject>
#include <QString>

namespace sicnu::teaching_admin {

/// Same verdict string as sicnu::cli::kUnavailableVerdict — an unavailable
/// row carries no score, only a typed reason.
inline constexpr const char *kTeachingUnavailableVerdict = "unavailable";

struct GraderCliConfig
{
    QString cliPath;    ///< empty ⇒ resolveGraderCli() (env / executable dir)
    QString labIdOrRulesPath; ///< lab id or `<lab_id>.rules.json` path
    qint64 maxBytes = 64 * 1024 * 1024; ///< forwarded as --max-bytes (>0 only)
    int timeoutMs = 120000;
};

/// Typed outcome of one grade over the CLI authority.
struct GraderCliGrade
{
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;

    QString status;  ///< "pass" | "fail" | "unverifiable" | "error" | "unavailable"
    QString verdict; ///< grader verdict, verbatim when the grader ran
    double score = -1.0; ///< < 0 when no score exists (never fabricated)
    QString reportDigest;   ///< transcript "digest" (sha256 of canonical body)
    QString topDeduction;   ///< first deduction assertion_id, when any
    QString unavailableReason; ///< mandatory when status == "unavailable"
    QString message;

    QJsonObject toJson() const;
};

/// Resolve the grading CLI: explicit path, else env SICNU_GEO_RS_CLI, else
/// `sicnu_geo_rs_cli` beside the running executable. Empty when none exists —
/// callers must degrade to typed unavailable, never to a fake score.
QString resolveGraderCli( const QString &explicitPath = QString() );

/// Grade one artifact through the real CLI authority (process-isolated twin
/// of scripts/run_classroom_batch.py's canonical argv). Never throws.
GraderCliGrade gradeViaCli( const GraderCliConfig &cfg, const QString &artifactPath );

/// Pure transcript-contract mapper — the ONE (exit code, transcript) → grade
/// mapping shared by BOTH paths: the CLI process shell (gradeViaCli) and an
/// in-process caller that already holds a sicnu.lab.grade/1 document
/// ({schema, digest, generated_utc, report}, e.g. emitted by
/// OutputVerifier::LabGradeResult::toJson). Cross-checks the exit contract
/// (0 pass / 1 fail / 2 usage / 3 unverifiable) against the transcript
/// verdict; a broken authority is refused as typed unavailable, never
/// trusted. Never throws.
GraderCliGrade gradeFromTranscript( int exitCode, const QJsonObject &transcript,
                                    const QString &usageText = QString() );

/// GradeCallable factory for runBatchAssessment: unavailable outcomes become
/// typed unavailable rows; grader verdicts become graded rows verbatim.
GradeCallable cliGradeCallable( const GraderCliConfig &cfg );

} // namespace sicnu::teaching_admin
