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
#include <string_view>

namespace sicnu::cli {

/// Stable symbol for a published exit code (docs/headless/README.md
/// "Exit codes (stable contract)"). Returns "GENERIC_ERROR" for unknown
/// values so the [E-n:SYMBOL] anchor never prints empty.
std::string_view exitCodeSymbol( int code );

/// The structured half of a command-level error message (Track 14 WP-B):
/// expected/actual when the failure is a mismatch, plus the suggested
/// action. The one-sentence reason keeps living in finish()'s
/// errorMessage — together they form the published four-tuple
/// code + reason + expected/actual + hint.
struct CliErrorDetails
{
    int exitCode = 1;
    std::string expected;
    std::string actual;
    std::string hint;

    /// "E-6:INVALID_INPUT" — anchored to the published contract table.
    std::string codeToken() const;
    Json::Value toJson() const;
};

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
    /// @p errorDetails is additive (Track 14 WP-B): when set, JSON mode gains
    /// an "error_details" object and text mode upgrades the bare stderr line
    /// to "[E-n:SYMBOL] <reason>; expected: …; actual: …; hint: …". Omit it
    /// and the published envelope/bare-line shapes are byte-identical to the
    /// pre-Track-14 contract.
    int finish( bool ok, const std::string &command, Json::Value data, int exitCode,
                const Json::Value &diagnostics = Json::Value( Json::nullValue ),
                const std::string &errorMessage = {},
                const CliErrorDetails *errorDetails = nullptr ) const;
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
