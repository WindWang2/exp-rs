// src/cli/cli_tool_commands.cpp — Surface-11 CLI discovery & batch commands

#include "cli_tool_commands.h"

#include "agent/tool_catalog/surface_registry.h"
#include "cli_batch_runner.h"
#include "exprs/exit_codes.h"

#include <json/json.h>

#include <algorithm>
#include <iostream>
#include <map>

using namespace sicnu::cli;
namespace exprs_ns = exprs;
namespace surface = sicnu::agent::tool_catalog;

namespace {

bool takeFlagLocal( QStringList &args, const QString &name )
{
    if ( !args.contains( name ) )
        return false;
    args.removeAll( name );
    return true;
}

QString takeValueLocal( QStringList &args, const QString &name, bool &present )
{
    const int index = args.indexOf( name );
    if ( index < 0 || index + 1 >= args.size() )
    {
        present = false;
        return {};
    }
    const QString value = args[index + 1];
    args.removeAt( index );
    args.removeAt( index );
    present = true;
    return value;
}

struct GlobalFlagsLocal
{
    bool json = false;
    bool jsonLines = false;
    bool quiet = false;
    bool progressJson = false;
};

GlobalFlagsLocal extractGlobalFlagsLocal( QStringList &args )
{
    GlobalFlagsLocal flags;
    flags.json = takeFlagLocal( args, "--json" );
    flags.jsonLines = takeFlagLocal( args, "--json-lines" );
    flags.quiet = takeFlagLocal( args, "--quiet" );
    flags.progressJson = takeFlagLocal( args, "--progress-json" );
    if ( flags.jsonLines )
        flags.json = true;
    return flags;
}

const char *sourceToString( surface::SurfaceToolSource source )
{
    switch ( source )
    {
        case surface::SurfaceToolSource::MetaProtocol:
            return "meta";
        case surface::SurfaceToolSource::DataPlatform:
            return "data-platform";
        case surface::SurfaceToolSource::Catalog:
            return "catalog";
    }
    return "catalog";
}

Json::Value toolEntry( const surface::SurfaceTool &tool, bool withSchema )
{
    Json::Value entry( Json::objectValue );
    entry["name"] = tool.name;
    entry["description"] = tool.description;
    entry["family"] = tool.family;
    entry["source"] = sourceToString( tool.source );
    if ( withSchema )
        entry["input_schema"] = tool.inputSchema;
    return entry;
}

bool matchesQuery( const surface::SurfaceTool &tool, const std::string &needle )
{
    if ( needle.empty() )
        return true;
    const auto contains = [&needle]( const std::string &haystack ) {
        return haystack.find( needle ) != std::string::npos;
    };
    return contains( tool.name ) || contains( tool.description ) || contains( tool.family );
}

int commandToolsListOrSearch( QStringList args, bool search, const CliIO &io )
{
    const GlobalFlagsLocal flags = extractGlobalFlagsLocal( args );
    std::string needle;
    if ( search )
    {
        if ( args.isEmpty() )
            return io.finish( false, "tools", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "usage: tools search <query>" );
        needle = args.takeFirst().toStdString();
    }
    const bool withSchema = takeFlagLocal( args, "--schema" );
    bool familyPresent = false;
    const QString familyFilter = takeValueLocal( args, "--family", familyPresent );
    bool sourcePresent = false;
    const QString sourceFilter = takeValueLocal( args, "--source", sourcePresent );
    if ( !args.isEmpty() )
        return io.finish( false, "tools", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "unknown argument: " + args.first().toStdString() );

    std::vector<surface::SurfaceTool> tools = surface::collectSurfaceTools();
    std::vector<const surface::SurfaceTool *> selected;
    for ( const surface::SurfaceTool &tool : tools )
    {
        if ( !matchesQuery( tool, needle ) )
            continue;
        if ( familyPresent && tool.family != familyFilter.toStdString() )
            continue;
        if ( sourcePresent && sourceToString( tool.source ) != sourceFilter.toStdString() )
            continue;
        selected.push_back( &tool );
    }

    Json::Value data( Json::objectValue );
    Json::Value entries( Json::arrayValue );
    for ( const surface::SurfaceTool *tool : selected )
        entries.append( toolEntry( *tool, withSchema ) );
    data["tools"] = entries;
    data["count"] = static_cast<Json::ArrayIndex>( selected.size() );

    if ( !flags.json && !flags.quiet )
    {
        for ( const surface::SurfaceTool *tool : selected )
            std::cout << tool->name << "\t" << tool->family << "\t" << tool->description << "\n";
    }
    return io.finish( true, "tools", data, 0 );
}

int commandToolsSchema( QStringList args, const CliIO &io )
{
    const GlobalFlagsLocal flags = extractGlobalFlagsLocal( args );
    if ( args.isEmpty() )
        return io.finish( false, "tools", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "usage: tools schema <tool-id>" );
    const std::string toolId = args.takeFirst().toStdString();
    const auto tool = surface::findSurfaceTool( toolId );
    if ( !tool )
        return io.finish( false, "tools", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::MissingDependency ),
                          {}, "unknown tool: " + toolId );

    Json::Value data = toolEntry( *tool, true );
    if ( !flags.json && !flags.quiet )
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::cout << Json::writeString( builder, data ) << "\n";
    }
    return io.finish( true, "tools", data, 0 );
}

// ---------------------------------------------------------------------------
// batch
// ---------------------------------------------------------------------------
int commandBatchRun( QStringList args, const CliIO &io )
{
    const GlobalFlagsLocal flags = extractGlobalFlagsLocal( args );
    if ( args.isEmpty() )
        return io.finish( false, "batch", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "usage: batch run <manifest.json|jsonl> [--fail-fast] [--dry-run] "
                              "[--result-index <path>] [--var k=v ...]" );
    const QString path = args.takeFirst();

    batch::Options options;
    options.failFast = takeFlagLocal( args, "--fail-fast" );
    options.dryRun = takeFlagLocal( args, "--dry-run" );
    bool indexPresent = false;
    options.resultIndexPath = takeValueLocal( args, "--result-index", indexPresent ).toStdString();
    while ( args.contains( "--var" ) )
    {
        bool present = false;
        const QString pair = takeValueLocal( args, "--var", present );
        if ( !present )
            break;
        const int equals = pair.indexOf( '=' );
        if ( equals <= 0 )
            return io.finish( false, "batch", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "--var expects key=value, got: " + pair.toStdString() );
        options.extraVariables.emplace_back( pair.left( equals ).toStdString(),
                                             pair.mid( equals + 1 ).toStdString() );
    }
    if ( !args.isEmpty() )
        return io.finish( false, "batch", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "unknown argument: " + args.first().toStdString() );

    batch::Callbacks callbacks;
    callbacks.progress = [&io]( int step, int total, double fraction, const std::string &message ) {
        io.reportProgress( step, total, fraction, message );
    };
    callbacks.log = [&io]( const std::string &level, const std::string &message ) {
        io.reportLog( level, message );
    };
    callbacks.isCancelled = []() { return cliIsInterrupted(); };

    std::string error;
    batch::Outcome outcome = batch::runManifestFile( path.toStdString(), options, callbacks, &error );
    if ( !error.empty() )
        return io.finish( false, "batch", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, error );

    Json::Value data( Json::objectValue );
    data["manifest"] = path.toStdString();
    data["total"] = static_cast<Json::ArrayIndex>( outcome.records.size() );
    Json::Value counts( Json::objectValue );
    int countsOk = 0, countsFailed = 0, countsSkipped = 0, countsCancelled = 0;
    Json::Value records( Json::arrayValue );
    for ( const batch::TaskRecord &record : outcome.records )
    {
        Json::Value line( Json::objectValue );
        line["index"] = record.index;
        line["id"] = record.id;
        line["operator"] = record.operatorId;
        line["status"] = record.status;
        line["exit_code"] = record.exitCode;
        if ( !record.error.empty() )
            line["error"] = record.error; // already redacted by the runner
        line["duration_ms"] = static_cast<Json::Int64>( record.durationMs );
        records.append( line );
        if ( record.status == "ok" ) ++countsOk;
        else if ( record.status == "failed" ) ++countsFailed;
        else if ( record.status == "cancelled" ) ++countsCancelled;
        else ++countsSkipped;
    }
    counts["ok"] = countsOk;
    counts["failed"] = countsFailed;
    counts["skipped"] = countsSkipped;
    counts["cancelled"] = countsCancelled;
    data["counts"] = counts;
    data["cancelled"] = outcome.cancelled;
    if ( flags.jsonLines )
        data["records"] = records;
    else if ( flags.json )
        data["records"] = records;

    const int exitCode = outcome.exitCode;
    const std::string message = exitCode == 0
        ? std::string()
        : ( outcome.cancelled ? std::string( "batch cancelled" )
                              : std::string( "batch finished with failures" ) );
    return io.finish( exitCode == 0, "batch", data, exitCode, {}, message );
}

int commandBatchValidate( QStringList args, const CliIO &io )
{
    extractGlobalFlagsLocal( args );
    if ( args.isEmpty() )
        return io.finish( false, "batch", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "usage: batch validate <manifest.json|jsonl>" );
    const QString path = args.takeFirst();
    if ( !args.isEmpty() )
        return io.finish( false, "batch", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "unknown argument: " + args.first().toStdString() );

    batch::Options options;
    options.dryRun = true;
    batch::Callbacks callbacks;
    callbacks.isCancelled = []() { return cliIsInterrupted(); };

    std::string error;
    batch::Outcome outcome = batch::runManifestFile( path.toStdString(), options, callbacks, &error );
    if ( !error.empty() )
        return io.finish( false, "batch", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ),
                          {}, error );

    Json::Value data( Json::objectValue );
    data["manifest"] = path.toStdString();
    data["tasks"] = static_cast<Json::ArrayIndex>( outcome.records.size() );
    int unknownOperators = 0;
    for ( const batch::TaskRecord &record : outcome.records )
        if ( record.exitCode == 5 ) // MissingDependency
            ++unknownOperators;
    data["unknown_operators"] = unknownOperators;
    return io.finish( true, "batch", data, 0 );
}

} // namespace

namespace sicnu::cli {

int commandTools( QStringList args, const CliIO &io )
{
    const QString sub = args.isEmpty() ? "list" : args.takeFirst();
    if ( sub == "list" )
        return commandToolsListOrSearch( std::move( args ), false, io );
    if ( sub == "search" )
        return commandToolsListOrSearch( std::move( args ), true, io );
    if ( sub == "schema" )
        return commandToolsSchema( std::move( args ), io );
    return io.finish( false, "tools", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                      {}, "unknown tools subcommand: " + sub.toStdString()
                          + " (expected list|search|schema)" );
}

int commandBatch( QStringList args, const CliIO &io )
{
    const QString sub = args.isEmpty() ? "run" : args.takeFirst();
    if ( sub == "run" )
        return commandBatchRun( std::move( args ), io );
    if ( sub == "validate" )
        return commandBatchValidate( std::move( args ), io );
    return io.finish( false, "batch", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                      {}, "unknown batch subcommand: " + sub.toStdString()
                          + " (expected run|validate)" );
}

} // namespace sicnu::cli
