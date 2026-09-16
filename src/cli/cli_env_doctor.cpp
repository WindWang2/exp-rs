/***************************************************************************
 * cli_env_doctor.cpp — `env-doctor` subcommand (Deployment 11.0, F19).
 *
 * Composes the Qt-free geospatial environment pass (geospatial/doctor/
 * env_doctor.h) with the Qt-layer probes only the running binary can make:
 * runtime Qt version, platform plugin discovery, SSL backend availability.
 * Output is report-only: this command never enables or disables anything.
 ***************************************************************************/

#include "cli_env_doctor.h"

#include "geospatial/doctor/env_doctor.h"

#include "exprs/exit_codes.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>

#include <json/json.h>

#include <iostream>
#include <string>
#include <vector>

namespace exprs_ns = exprs;

namespace sicnu::cli
{
namespace
{

void removeFlag( QStringList &args, const QString &name )
{
    args.removeAll( name );
}

} // namespace

int commandEnvDoctor( QStringList args, const CliIO &io )
{
    removeFlag( args, QStringLiteral( "--json" ) ); // consumed by main's CliIO wiring
    if ( !args.isEmpty() )
    {
        const std::string unknown = args.front().toStdString();
        return io.finish( false, "env-doctor", {},
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "unknown env-doctor argument: " + unknown
                                  + " (supported: --json)" );
    }
    const bool asJson = io.json || io.jsonLines;

    sicnu::geo::envcheck::EnvCheckOptions options;
    options.applicationDir = QCoreApplication::applicationDirPath().toStdString();

    sicnu::geo::envcheck::EnvDoctorReport report = runEnvironmentDoctor( options );

    // ---- Qt-layer probes appended to the same checks array ----
    {
        Json::Value detail;
        detail["runtime"] = qVersion();
        report.append( "ok", "qt.version", std::string( "Qt " ) + qVersion(), detail );
    }

    // Platform plugin discovery: probe the Qt library paths plus the classic
    // deployment-relative dirs, name every candidate probed (F19 Oracle 3).
    {
        Json::Value detail;
        Json::Value probed( Json::arrayValue );
        std::string found;
        std::vector< QString > candidates;
        const QStringList libPaths = QCoreApplication::libraryPaths();
        for ( const QString &lp : libPaths )
            candidates.push_back( lp + QStringLiteral( "/platforms" ) );
        if ( !options.applicationDir.empty() )
        {
            const QDir appDir( QString::fromStdString( options.applicationDir ) );
            candidates.push_back( appDir.absoluteFilePath( QStringLiteral( "../plugins/platforms" ) ) );
            candidates.push_back( appDir.absoluteFilePath( QStringLiteral( "platforms" ) ) );
        }
        const QString qtPluginPath = qEnvironmentVariable( "QT_PLUGIN_PATH" );
        if ( !qtPluginPath.isEmpty() )
        {
            const QStringList parts = qtPluginPath.split( QDir::listSeparator(), Qt::SkipEmptyParts );
            for ( const QString &p : parts )
                candidates.push_back( QDir( p ).absoluteFilePath( QStringLiteral( "platforms" ) ) );
        }
        // The plugin that matters is the one QT_QPA_PLATFORM selects
        // (offscreen for every headless classroom flow); common platform
        // plugins are accepted as fallbacks so a GUI deployment also passes.
        const QString platform = qEnvironmentVariable( "QT_QPA_PLATFORM" );
        QStringList platformPlugins;
        if ( !platform.isEmpty() )
            platformPlugins << QStringLiteral( "libq%1.so" ).arg( platform )
                            << QStringLiteral( "q%1.dll" ).arg( platform )
                            << QStringLiteral( "libq%1.dylib" ).arg( platform );
#if defined( _WIN32 )
        platformPlugins << QStringLiteral( "qwindows.dll" );
#elif defined( __APPLE__ )
        platformPlugins << QStringLiteral( "libqcocoa.dylib" );
#else
        platformPlugins << QStringLiteral( "libqoffscreen.so" )
                        << QStringLiteral( "libqxcb.so" );
#endif
        for ( const QString &candidate : candidates )
        {
            probed.append( candidate.toStdString() );
            if ( !found.empty() || !QDir( candidate ).exists() )
                continue;
            // A platforms DIRECTORY without the actual plugin is the classic
            // broken-windeployqt state; require the platform plugin itself.
            for ( const QString &plugin : platformPlugins )
            {
                if ( QDir( candidate ).exists( plugin ) )
                {
                    found = candidate.toStdString();
                    detail["plugin"] = plugin.toStdString();
                    break;
                }
            }
        }
        detail["probed"] = probed;
        if ( !found.empty() )
        {
            detail["resolved"] = found;
            report.append( "ok", "qt.platform.plugins", "platform plugin found", detail );
        }
        else
        {
            report.append( "error", "qt.platform.plugins", "no Qt platform plugin directory found",
                    detail, "diagnostic.env.platform_plugin_missing" );
        }
    }

    // SSL backend availability: local QLibrary loads only, no network.
    {
        Json::Value detail;
        Json::Value probed( Json::arrayValue );
        std::string found;
        const std::vector< QString > candidates
#if defined( _WIN32 )
        {
            QStringLiteral( "libssl-3-x64" ), QStringLiteral( "ssleay32" ),
            QStringLiteral( "libssl-1_1-x64" )
        }
#else
        {
            QStringLiteral( "libssl.so.3" ), QStringLiteral( "libssl.so.1.1" )
        }
#endif
        ;
        for ( const QString &name : candidates )
        {
            probed.append( name.toStdString() );
            if ( !found.empty() )
                continue;
            QLibrary lib( name );
            if ( lib.load() )
            {
                found = name.toStdString();
                lib.unload();
            }
        }
        detail["probed"] = probed;
        if ( !found.empty() )
        {
            detail["loaded"] = found;
            report.append( "ok", "ssl.backend", "SSL runtime library loadable", detail );
        }
        else
        {
            report.append( "warning", "ssl.backend", "no loadable SSL runtime library found",
                    detail, "diagnostic.env.ssl_library_missing" );
        }
    }

    const std::string verdict = report.errorCount > 0   ? "broken"
                                : report.warningCount > 0 ? "degraded"
                                                        : "healthy";
    const int exitCode = report.errorCount > 0 || report.warningCount > 0
                             ? exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure )
                             : 0;

    if ( asJson )
    {
        Json::Value data = report.toJson();
        data["verdict"] = verdict;
        return io.finish( report.errorCount == 0, "env-doctor", data, exitCode );
    }

    // Human report: one line per finding, worst state drives the verdict line.
    for ( const Json::Value &check : report.checks )
    {
        const std::string severity = check["severity"].asString();
        const char *tag = "OK  ";
        if ( severity == "warning" )
            tag = "WARN";
        else if ( severity == "error" )
            tag = "ERROR";
        else if ( severity == "info" )
            tag = "INFO";
        std::string line = std::string( tag ) + " [" + check["check"].asString() + "] "
                           + check["message"].asString();
        if ( check.isMember( "diagnostic" ) )
            line += " (" + check["diagnostic"].asString() + ")";
        std::cout << line << "\n";
    }
    std::cout << "ENV DOCTOR " << verdict << " (" << report.okCount << " ok, "
              << report.infoCount << " info, " << report.warningCount << " warnings, "
              << report.errorCount << " errors)\n";
    return exitCode;
}

} // namespace sicnu::cli
