// src/cli/cli_passport_commands.cpp — RS14-01 Scientific Data Passport command.
//
// Read-only projection: collects facts from one raster file (GDAL metadata
// only), resolves the unified scientific state and prints it either as the
// machine-readable sicnu.asset_state.v1 JSON envelope or as the teaching
// plain-text view. Optional --diff compares against a previously exported
// passport (sicnu.asset_state_diff.v1).

#include "cli_passport_commands.h"

#include "scientific_state/asset_state_diff.h"
#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_resolver.h"
#include "scientific_state/gdal/gdal_state_facts.h"
#include "scientific_state/teaching_view.h"

#include "exprs/exit_codes.h"

#include <QFile>
#include <QIODevice>

#include <json/json.h>

#include <iostream>

namespace exprs_ns = exprs;

namespace sicnu::cli {

namespace {

bool takeFlag( QStringList &args, const QString &flag )
{
    for ( int i = 0; i < args.size(); ++i )
    {
        if ( args.at( i ) == flag )
        {
            args.removeAt( i );
            return true;
        }
    }
    return false;
}

QString takeValue( QStringList &args, const QString &flag, bool *present )
{
    *present = false;
    for ( int i = 0; i + 1 < args.size(); ++i )
    {
        if ( args.at( i ) == flag )
        {
            *present = true;
            const QString value = args.at( i + 1 );
            args.removeAt( i );
            args.removeAt( i );
            return value;
        }
    }
    return QString();
}

bool readTextFile( const QString &path, std::string *out, std::string *error )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
    {
        *error = "cannot read file: " + path.toStdString();
        return false;
    }
    *out = file.readAll().toStdString();
    return true;
}

} // namespace

int commandPassport( QStringList args, const CliIO &io )
{
    const bool json = takeFlag( args, QStringLiteral( "--json" ) );
    const bool teaching = takeFlag( args, QStringLiteral( "--teaching" ) );
    bool diffPresent = false;
    const QString diffPath = takeValue( args, QStringLiteral( "--diff" ), &diffPresent );
    bool pathPresent = false;
    const QString path = takeValue( args, QStringLiteral( "--path" ), &pathPresent );

    if ( !pathPresent || path.isEmpty() )
        return io.finish( false, "passport", {},
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "usage: passport --path <file> [--json] [--teaching] "
                              "[--diff <passport.json>] (--json wins over --teaching)" );
    if ( !args.isEmpty() )
        return io.finish( false, "passport", {},
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "unknown argument: " + args.first().toStdString() );

    sicnu::state::GdalFactsError collectError;
    const std::optional<sicnu::state::DatasetFacts> facts =
        sicnu::state::collectDatasetFacts( path.toStdString(), &collectError );
    if ( !facts.has_value() )
        return io.finish( false, "passport", {},
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {},
                          collectError.code + ": " + collectError.detail + " (" +
                              collectError.path + ")" );

    sicnu::state::StateResolutionInput input;
    input.dataset = *facts;
    input.sourcePath = path.toStdString();
    const sicnu::state::RemoteSensingAssetState state =
        sicnu::state::resolveAssetState( input ).state;

    std::optional<sicnu::state::StateDiff> diff;
    if ( diffPresent && diffPath.isEmpty() )
        return io.finish( false, "passport", {},
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "--diff requires a passport document path" );
    if ( diffPresent )
    {
        std::string readError;
        std::string beforeText;
        if ( !readTextFile( diffPath, &beforeText, &readError ) )
            return io.finish( false, "passport", {},
                              exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, readError );
        sicnu::state::RemoteSensingAssetState before;
        sicnu::state::AssetStateError parseError;
        if ( !sicnu::state::assetStateFromJson( beforeText, before, parseError ) )
            return io.finish( false, "passport", {},
                              exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ),
                              {}, "invalid passport document: " + parseError.message );
        diff = sicnu::state::diffStates( before, state );
    }

    if ( teaching && !json )
    {
        const sicnu::state::TeachingSummary summary =
            sicnu::state::renderTeachingSummary( state );
        std::cout << sicnu::state::teachingSummaryToPlainText( summary );
        if ( diff.has_value() )
        {
            std::cout << "\nDiff against " << diffPath.toStdString() << ":\n";
            for ( const sicnu::state::FieldDiff &field : diff->diffs )
            {
                std::cout << "  [" << field.kind << "] " << field.path;
                if ( field.kind == "claim_changed" )
                    std::cout << " (" << field.before << " -> " << field.after << ")";
                std::cout << "\n";
            }
            if ( diff->diffs.empty() )
                std::cout << "  (no differences)\n";
        }
        std::cout.flush();
        return io.finish( true, "passport", {},
                          exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok ) );
    }

    Json::Value data( Json::objectValue );
    data["passport"] = sicnu::state::assetStateToJson( state );
    if ( diff.has_value() )
        data["diff"] = sicnu::state::stateDiffToJson( *diff );
    return io.finish( true, "passport", std::move( data ),
                      exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok ) );
}

} // namespace sicnu::cli
