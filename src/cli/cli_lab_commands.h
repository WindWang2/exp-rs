/***************************************************************************
 * src/cli/cli_lab_commands.h — `lab` command: teaching auto-grader (D4)
 *
 *   sicnu_geo_rs_cli lab --lab <id|.rules.json> --grade <artifact>
 *                       [--out report.json] [--max-bytes <n>]
 *
 * Prints the JSON grade transcript ({schema, digest, generated_utc, report})
 * and maps the grading outcome onto the documented exit-code contract:
 *   0 pass · 1 fail · 2 usage · 3 unverifiable
 * (values coincide with exprs::ExitCode Ok/GenericError/ValidationFailure/
 * ExecutionFailure).  This file is owned by D4; D7 appends only a `--batch`
 * branch on top of OutputVerifier::gradeArtifact().
 ***************************************************************************/
#pragma once

#include <QStringList>

namespace sicnu::cli {

struct CliIO;

/// Dispatches `lab ...`; returns the process exit code.
int commandLab( QStringList arguments, const CliIO &io );

} // namespace sicnu::cli
