/***************************************************************************
 * src/cli/cli_agent_ops_commands.cpp — `session` command implementation.
 *
 * Flag parsing only: every decision below the envelope belongs to the
 * shared OpsDriver / OperationsCoordinator / agent_loop stack.
 ***************************************************************************/
#include "cli_agent_ops_commands.h"

#include "cli_commands.h"

#include "agent/harness/harness_error.h"
#include "agent_ops/ops_driver.h"
#include "exprs/exit_codes.h"

#include <json/json.h>

#include <QFile>
#include <QString>

#include <memory>

namespace exprs_ns = exprs;

namespace sicnu::cli {

namespace {

std::string qstr( const QString &text )
{
    return text.toStdString();
}

/// Actions the driver does not accept (guard against typos early).
bool isKnownAction( const QString &action )
{
    static const QStringList kActions = { "run",       "resume",       "reconcile",
                                          "status",    "timeline",     "export",
                                          "pause",     "cancel",       "clear-pause",
                                          "clear-cancel",              "approve-repair",
                                          "clear_pause", "clear_cancel", "actions" };
    return kActions.contains( action );
}

Json::Value parseRefs( const QString &raw, bool *ok )
{
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string error;
    Json::CharReader *reader = builder.newCharReader();
    const std::string text = qstr( raw );
    *ok = reader->parse( text.data(), text.data() + text.size(), &parsed, &error );
    delete reader;
    return parsed;
}

/// Reads the repair-approval token document. The token is a small JSON
/// object minted by `session approve-repair`; anything missing, unparseable
/// or non-object is a typed refusal — an approval must never be silently
/// dropped on its way to the driver (CT-B, R20-03).
bool readApprovalTokenFile( const QString &path, Json::Value &token, std::string &error )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        error = "cannot read token file '" + qstr( path ) + "'";
        return false;
    }
    // Bounded read: a token is a small sidecar; a planted huge file must not
    // be buffered whole (same cap discipline as the workflow kernel).
    const QByteArray raw = file.read( 1 * 1024 * 1024 + 1 );
    if ( raw.size() > 1 * 1024 * 1024 )
    {
        error = "token file '" + qstr( path ) + "' exceeds the 1 MiB cap";
        return false;
    }
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    std::string parseError;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &token,
                         &parseError ) )
    {
        error = "token file '" + qstr( path ) + "' is not valid JSON: " + parseError;
        return false;
    }
    if ( !token.isObject() )
    {
        error = "token file '" + qstr( path ) + "' must contain a JSON object";
        return false;
    }
    return true;
}

} // namespace

sicnu::agent_ops::OpsDriver *defaultCliAgentOpsDriver()
{
    static sicnu::agent_ops::OpsDriver driver{ sicnu::agent_ops::OpsDriver::Options{} };
    return &driver;
}

namespace {

constexpr const char *kSessionUsage =
    "usage: session <action> [flags] (run|resume|reconcile|status|timeline|export|"
    "pause|cancel|clear-pause|clear-cancel|approve-repair|clear_pause|clear_cancel|actions)";

/// Track 14 (WP-B): session's parse-time four-tuple construction site.
int sessionError( const CliIO &io, const std::string &message, int exitCode,
                  const std::string &expected = {}, const std::string &actual = {} )
{
    sicnu::cli::CliErrorDetails details;
    details.exitCode = exitCode;
    details.expected = expected;
    details.actual = actual;
    details.hint = kSessionUsage;
    return io.finish( false, "session", {}, exitCode, {}, message, &details );
}

} // namespace

int commandAgentSession( QStringList arguments, const CliIO &io,
                         sicnu::agent_ops::OpsDriver *driver )
{
    if ( arguments.isEmpty() )
        return sessionError( io, kSessionUsage,
                             exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                             "run|resume|reconcile|status|timeline|export|pause|cancel|"
                             "clear-pause|clear-cancel|approve-repair|clear_pause|"
                             "clear_cancel|actions" );

    const QString action = arguments.takeFirst();
    if ( !isKnownAction( action ) )
        return sessionError( io, "unknown session action '" + action.toStdString() + "'",
                             exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                             "run|resume|reconcile|status|timeline|export|pause|cancel|"
                             "clear-pause|clear-cancel|approve-repair|clear_pause|"
                             "clear_cancel|actions",
                             action.toStdString() );

    // Normalize CLI dashes to the wire's snake_case action names.
    QString wireAction = action;
    wireAction.replace( QLatin1Char( '-' ), QLatin1Char( '_' ) );

    Json::Value args( Json::objectValue );
    args["action"] = qstr( wireAction );
    for ( int i = 0; i < arguments.size(); ++i )
    {
        const QString flag = arguments.at( i );
        auto next = [&]( const char *what ) -> QString {
            if ( i + 1 >= arguments.size() )
            {
                // Missing value: let the typed MISSING_ARGS path report it.
                return QString();
            }
            // Never eat another flag as this flag's value: `--approve --json`
            // leaves `--json` for its own parse (the cli_commands.cpp
            // takeValue rule), so an approval is never silently dropped.
            if ( arguments.at( i + 1 ).startsWith( QStringLiteral( "--" ) ) )
                return QString();
            return arguments.at( ++i );
        };
        if ( flag == QStringLiteral( "--goal" ) )
            args["goal"] = qstr( next( "goal" ) );
        else if ( flag == QStringLiteral( "--intent" ) )
            args["intent"] = qstr( next( "intent" ) );
        else if ( flag == QStringLiteral( "--mode" ) )
            args["mode"] = qstr( next( "mode" ) );
        else if ( flag == QStringLiteral( "--journal-dir" ) )
            args["journal_directory"] = qstr( next( "journal-dir" ) );
        else if ( flag == QStringLiteral( "--session-id" ) )
            args["session_id"] = qstr( next( "session-id" ) );
        else if ( flag == QStringLiteral( "--domain" ) )
            args["domain"] = qstr( next( "domain" ) );
        else if ( flag == QStringLiteral( "--role" ) )
            args["role"] = qstr( next( "role" ) );
        else if ( flag == QStringLiteral( "--refs" ) )
        {
            bool ok = false;
            const Json::Value refs = parseRefs( next( "refs" ), &ok );
            if ( !ok )
                return sessionError( io, "--refs must be a JSON object",
                                     exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                                     "--refs <json-object>" );
            args["refs"] = refs;
        }
        else if ( flag == QStringLiteral( "--approve" ) ||
                  flag == QStringLiteral( "--approve-pending-repair" ) )
        {
            // CT-B (R20-03): the bare-bool spellings armed nothing —
            // args["approve"] had zero consumers and the legacy
            // approve_pending_repair is exactly what the driver refuses.
            // Refuse at parse time and point at the token channel: mint via
            // `session approve-repair`, arm via --repair-approval.
            (void)next( "approve" );
            return sessionError(
                io, "a bare --approve arms nothing: mint the token with `session "
                    "approve-repair`, then pass it with --repair-approval <token.json>",
                exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                "--repair-approval <token.json> [--approval-now-ms <ms>]",
                flag.toStdString() );
        }
        else if ( flag == QStringLiteral( "--repair-approval" ) )
        {
            const QString tokenPath = next( "repair-approval" );
            Json::Value token( Json::objectValue );
            std::string tokenError;
            if ( !readApprovalTokenFile( tokenPath, token, tokenError ) )
                return sessionError(
                    io, tokenError, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                    "--repair-approval <token.json>", tokenPath.toStdString() );
            args["repair_approval"] = token;
        }
        else if ( flag == QStringLiteral( "--approval-now-ms" ) )
        {
            bool parsed = false;
            const qlonglong value = next( "approval-now-ms" ).toLongLong( &parsed );
            if ( !parsed )
                return sessionError( io, "--approval-now-ms needs an integer",
                                     exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                                     "--approval-now-ms <ms>", "--approval-now-ms" );
            args["approval_now_ms"] = Json::Int64( value );
        }
        else
        {
            return sessionError( io, "unknown flag '" + flag.toStdString() + "'",
                                 exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                                 "--goal|--intent|--mode|--journal-dir|--session-id|"
                                 "--domain|--role|--refs|--repair-approval|--approval-now-ms",
                                 flag.toStdString() );
        }
    }

    if ( !driver )
        return sessionError( io, "AGENT_OPS_UNAVAILABLE",
                             exprs_ns::exitCodeValue( exprs_ns::ExitCode::RuntimeUnavailable ) );

    const Json::Value doc = driver->apply( qstr( wireAction ), args );

    // Typed error → exit code (the envelope still carries the full doc).
    const bool ok = doc.get( "ok", false ).asBool();
    const std::string error = doc.get( "error", "" ).asString();
    const std::string stopReason =
        doc.get( "delivery", Json::Value( Json::objectValue ) )
            .get( "stop_reason", "" )
            .asString();
    if ( ok )
        return io.finish( true, "session", doc,
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok ) );

    // Cancellation and pause are user-initiated holds, not failures.
    if ( stopReason == "CANCELLED" || error == "PAUSED" )
        return io.finish( false, "session", doc,
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::Cancelled ), {},
                          error );

    // Reconciliation / journal-verdict refusals and argument errors: the
    // "unknown is never resumable" family (INDETERMINATE_STATE) belongs to
    // the SAME typed family as the corrupted journal — scripts can rely on
    // exit 2 meaning "the session/journal state refuses this operation".
    // R6 convergence (#1394 item 4): the refusal -> exit-code mapping lives
    // ONCE in the internal error model (harness_error), not in a private
    // CLI if-chain. The stop_reason CANCELLED/PAUSED holds above stay
    // user-initiated-exit-4 here.
    return io.finish( false, "session", doc,
                      sicnu::agent::harness::cliExitCodeForSessionRefusal( error ), {},
                      error );
}

} // namespace sicnu::cli
