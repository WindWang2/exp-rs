/***************************************************************************
 * cli_env_doctor.h — `env-doctor` subcommand (Deployment 11.0, F19).
 *
 *   sicnu_geo_rs_cli env-doctor [--json]
 *
 * First-run environment self-check: reports (never toggles) the offline gate
 * state plus Qt/GDAL/PROJ/runtime-data/temp/unicode/SSL/platform-plugin
 * health of the binary that will do the work. Exit contract:
 *   0 healthy (ok/info findings only)
 *   2 degraded (>=1 warning)  /  broken (>=1 error) — ValidationFailure is
 *     used for both non-healthy outcomes; the JSON envelope's data.checks
 *     and data.verdict distinguish degraded vs broken.
 * Findings carry curated ids from data/help/diagnostics.json (family "env").
 ***************************************************************************/
#pragma once

#include <QStringList>

#include "cli_commands.h"

namespace sicnu::cli
{

/// Runs the environment pass and prints the text/JSON report; returns the
/// process exit code per the contract above.
int commandEnvDoctor( QStringList args, const CliIO &io );

} // namespace sicnu::cli
