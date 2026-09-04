/***************************************************************************
 * src/cli/cli_commands.h — Headless CLI 3.0 command layer
 *
 * Adds a subcommand surface on top of the legacy flag-based CLI (which is
 * preserved verbatim). Designed for scripts and agents:
 *
 *   sicnu_geo_rs_cli <command> [sub] [options] [args]
 *
 * Commands: algorithms | run | pipeline | workflow | plugin | models |
 *           catalog | data-providers
 * Global output flags (accepted by every command):
 *   --json          single JSON envelope on stdout (progress -> stderr)
 *   --json-lines    NDJSON result records on stdout
 *   --quiet         suppress non-essential output
 *   --progress-json NDJSON progress records on stderr
 *
 * Exit codes are the exprs::ExitCode contract (exprs/exit_codes.h).
 ***************************************************************************/
#pragma once

#include <QStringList>

#include <json/json.h>

#include <string>

namespace sicnu::cli {

/// Output mode flags shared by every command.
struct CliIO
{
    bool json = false;
    bool jsonLines = false;
    bool quiet = false;
    bool progressJson = false;

    /// Progress/log sink honoring --progress-json / --json (stderr).
    void reportProgress( int stepIndex, int totalSteps, double stepProgress,
                         const std::string &message ) const;
    void reportLog( const std::string &level, const std::string &message ) const;

    /// Prints the final envelope to stdout and returns the mapped exit code.
    int finish( bool ok, const std::string &command, Json::Value data, int exitCode,
                const Json::Value &diagnostics = Json::Value( Json::nullValue ),
                const std::string &errorMessage = {} ) const;
};

/// True when @p firstArg is a CLI 3.0 command (used by main to route).
bool isCliCommand( const QString &firstArg );

/// Cooperative interruption flag (SIGINT/SIGTERM), shared with the legacy
/// pipeline runner.
bool cliIsInterrupted();

/// Dispatches a command from @p arguments (argv[1..]). Requires the caller
/// to have initialized the core services (QgsApplication, AlgorithmEngine,
/// JobEngine fallback, plugin runtime bootstrap). Returns the process exit
/// code.
int dispatchCliCommand( const QStringList &arguments, const CliIO &io );

} // namespace sicnu::cli
