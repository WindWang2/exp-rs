/***************************************************************************
 * src/cli/cli_agent_ops_commands.cpp — `session` command implementation.
 *
 * Flag parsing only: every decision below the envelope belongs to the
 * shared OpsDriver / OperationsCoordinator / agent_loop stack.
 ***************************************************************************/
#include "cli_agent_ops_commands.h"

#include "cli_commands.h"

#include "agent_ops/ops_driver.h"
#include "exprs/exit_codes.h"

#include <json/json.h>

#include <QString>

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

} // namespace

sicnu::agent_ops::OpsDriver *defaultCliAgentOpsDriver()
{
    static sicnu::agent_ops::OpsDriver driver{ sicnu::agent_ops::OpsDriver::Options{} };
    return &driver;
}

int commandAgentSession( QStringList arguments, const CliIO &io,
                         sicnu::agent_ops::OpsDriver *driver )
{
    if ( arguments.isEmpty() )
        return io.finish( false, "session", {},
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ), {},
                          "usage: session <action> [flags] (run|resume|reconcile|status|"
                          "timeline|export|pause|cancel|clear-pause|clear-cancel|"
                          "approve-repair|actions)" );

    const QString action = arguments.takeFirst();
    if ( !isKnownAction( action ) )
        return io.finish( false, "session", {},
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ), {},
                          "unknown session action '" + action.toStdString() + "'" );

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
                return io.finish( false, "session", {},
                                  exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ), {},
                                  "--refs must be a JSON object" );
            args["refs"] = refs;
        }
        else if ( flag == QStringLiteral( "--approve" ) ||
                  flag == QStringLiteral( "--approve-pending-repair" ) )
        {
            const QString value = next( "approve" );
            args["approve"] = value.isEmpty() || value == QStringLiteral( "true" );
            if ( flag == QStringLiteral( "--approve-pending-repair" ) )
                args["approve_pending_repair"] = args["approve"].asBool();
        }
        else
        {
            return io.finish( false, "session", {},
                              exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ), {},
                              "unknown flag '" + flag.toStdString() + "'" );
        }
    }

    if ( !driver )
        return io.finish( false, "session", {},
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::RuntimeUnavailable ), {},
                          "AGENT_OPS_UNAVAILABLE" );

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
    int exitCode = exprs_ns::exitCodeValue( exprs_ns::ExitCode::GenericError );
    if ( error == "MISSING_ARGS" || error == "UNKNOWN_MODE" || error == "UNKNOWN_SESSION" ||
         error == "UNKNOWN_ACTION" || error == "NO_SESSION" || error == "INVALID_ARGS" ||
         error.find( "AUTONOMY" ) != std::string::npos )
        exitCode = exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
    else if ( error == "SEAMS_UNAVAILABLE" )
        exitCode = exprs_ns::exitCodeValue( exprs_ns::ExitCode::MissingDependency );
    else if ( error == "SUBMITTED_RUN_EXISTS" || error == "DUPLICATE_SUBMIT_REFUSED" ||
              error == "RESUME_PAST_PLAN_SEAM" || error == "SESSION_RESUMABLE_USE_RESUME" ||
              error == "SESSION_GOAL_MISMATCH" || error == "CORRUPTED_OR_MISSING_JOURNAL" ||
              error == "INDETERMINATE_STATE" || error == "EMPTY_SESSION" ||
              error == "RESUME_REJECTED" )
        exitCode = exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
    return io.finish( false, "session", doc, exitCode, {}, error );
}

} // namespace sicnu::cli
