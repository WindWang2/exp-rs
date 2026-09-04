/***************************************************************************
 * src/cli/cli_commands.cpp
 ***************************************************************************/
#include "cli_commands.h"

#include "rs_pipeline_runner.h"

#include "exprs/exit_codes.h"
#include "exprs/external_process.h"
#include "exprs/plugin_discovery.h"
#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_manifest.h"
#include "exprs/plugin_package.h"
#include "exprs/plugin_registry.h"
#include "exprs/plugin_validator.h"
#include "exprs/version.h"
#include "exprs/workflow_schema.h"
#include "plugins/framework/data_provider_registry.h"
#include "plugins/framework/plugin_runtime_host.h"

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/runtime/model_runtime.h"
#include "processing/framework/algorithm_engine.h"
#include "processing/framework/algorithm_meta_store.h"
#include "operators/framework/rs_operator_error.h"
#include <csignal>
#include "processing/framework/atomic_algorithm_registry.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run_coordinator.h"
#include "workflow/workflow_run_lock.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <qgsproject.h>

#include <fstream>
#include <iostream>
#include <sstream>

using namespace sicnu::cli;
namespace operators = sicnu::operators;
namespace processing = sicnu::processing;
namespace exprs_ns = exprs;

namespace {

Json::Value parseJsonFile( const std::string &path, std::string &error )
{
    std::ifstream input( path );
    if ( !input )
    {
        error = "cannot open " + path;
        return Json::Value( Json::nullValue );
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    Json::Value parsed;
    Json::Reader reader;
    if ( !reader.parse( buffer.str(), parsed, false ) )
    {
        error = "invalid JSON in " + path + ": " + reader.getFormattedErrorMessages();
        return Json::Value( Json::nullValue );
    }
    return parsed;
}

Json::Value parseScalar( const std::string &text )
{
    Json::Value parsed;
    Json::Reader reader;
    if ( reader.parse( text, parsed, false ) )
        return parsed;
    return Json::Value( text );
}

bool takeFlag( QStringList &args, const QString &name )
{
    if ( !args.contains( name ) )
        return false;
    args.removeAll( name );
    return true;
}

QString takeValue( QStringList &args, const QString &name, bool &present )
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

struct GlobalFlags
{
    bool json = false;
    bool jsonLines = false;
    bool quiet = false;
    bool progressJson = false;
};

GlobalFlags extractGlobalFlags( QStringList &args )
{
    GlobalFlags flags;
    flags.json = takeFlag( args, "--json" );
    flags.jsonLines = takeFlag( args, "--json-lines" );
    flags.quiet = takeFlag( args, "--quiet" );
    flags.progressJson = takeFlag( args, "--progress-json" );
    if ( flags.jsonLines )
        flags.json = true;
    return flags;
}

// ---------------------------------------------------------------------------
// algorithms
// ---------------------------------------------------------------------------
int commandAlgorithms( QStringList args, const CliIO &io )
{
    extractGlobalFlags( args );
    QString sub = args.isEmpty() ? "list" : args.takeFirst();
    QString needle;
    if ( sub == "search" || sub == "schema" )
    {
        if ( args.isEmpty() )
        {
            std::cerr << "algorithms " << sub.toStdString() << " requires an argument\n";
            return io.finish( false, "algorithms", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ), {},
                              "missing search/schema argument" );
        }
        needle = args.takeFirst();
    }

    const auto &registry = processing::AtomicAlgorithmRegistry::instance();
    const auto descriptors = registry.listDescriptors();

    if ( sub == "schema" )
    {
        Json::Value schemaJson( Json::nullValue );
        std::string description;
        for ( const auto &descriptor : descriptors )
        {
            if ( descriptor.id == needle.toStdString() )
            {
                schemaJson = descriptor.toInputSchema();
                description = descriptor.description;
                break;
            }
        }
        if ( schemaJson.isNull() )
        {
            return io.finish( false, "algorithms", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::MissingDependency ),
                              {}, "unknown algorithm: " + needle.toStdString() );
        }
        Json::Value data( Json::objectValue );
        data["id"] = needle.toStdString();
        data["description"] = description;
        data["input_schema"] = schemaJson;
        return io.finish( true, "algorithms", data, 0 );
    }

    // list / search
    const std::string needleText = needle.toStdString();
    Json::Value data( Json::arrayValue );
    for ( const auto &descriptor : descriptors )
    {
        if ( !needleText.empty() )
        {
            const bool match = descriptor.id.find( needleText ) != std::string::npos
                               || descriptor.displayName.find( needleText ) != std::string::npos
                               || descriptor.description.find( needleText ) != std::string::npos
                               || descriptor.group.find( needleText ) != std::string::npos;
            if ( !match )
                continue;
        }
        Json::Value entry( Json::objectValue );
        entry["id"] = descriptor.id;
        entry["display_name"] = descriptor.displayName;
        entry["group"] = descriptor.group;
        entry["description"] = descriptor.description;
        entry["source"] = sicnu::plugins::PluginRuntimeHost::instance().isPluginOperator( descriptor.id )
                              ? "plugin"
                              : "builtin";
        data.append( entry );
    }
    return io.finish( true, "algorithms", data, 0 );
}

// ---------------------------------------------------------------------------
// run — execute one operator
// ---------------------------------------------------------------------------
int commandRun( QStringList args, const CliIO &io )
{
    extractGlobalFlags( args );
    if ( args.isEmpty() )
    {
        return io.finish( false, "run", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "usage: run <operator-id> [--param k=v ...] [--params-file f]" );
    }
    const std::string operatorId = args.takeFirst().toStdString();

    Json::Value params( Json::objectValue );
    while ( args.contains( "--param" ) )
    {
        bool present = false;
        const QString pair = takeValue( args, "--param", present );
        if ( !present )
            break;
        const int equals = pair.indexOf( '=' );
        if ( equals <= 0 )
        {
            return io.finish( false, "run", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "--param expects key=value, got: " + pair.toStdString() );
        }
        params[pair.left( equals ).toStdString()] = parseScalar( pair.mid( equals + 1 ).toStdString() );
    }
    if ( args.contains( "--params-file" ) )
    {
        bool present = false;
        const QString path = takeValue( args, "--params-file", present );
        if ( present )
        {
            std::string error;
            const Json::Value fileParams = parseJsonFile( path.toStdString(), error );
            if ( !error.empty() )
            {
                return io.finish( false, "run", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                                  {}, error );
            }
            for ( const std::string &key : fileParams.getMemberNames() )
                params[key] = fileParams[key];
        }
    }

    const auto adapter = processing::AtomicAlgorithmRegistry::instance().findAdapter( operatorId );
    if ( !adapter )
    {
        return io.finish( false, "run", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::MissingDependency ),
                          {}, "unknown algorithm: " + operatorId );
    }

    Json::Value result;
    try
    {
        result = adapter->execute(
            params,
            [&io]( int percent, const std::string &message ) {
                io.reportProgress( 1, 1, percent / 100.0, message );
            },
            []() { return sicnu::cli::cliIsInterrupted(); } );
    }
    catch ( const sicnu::operators::RSOperatorError &error )
    {
        const bool cancelled = error.code() == sicnu::operators::ErrorCode::Cancelled;
        Json::Value details( Json::objectValue );
        details["code"] = sicnu::operators::errorCodeToString( error.code() );
        details["message"] = error.message();
        return io.finish( false, "run", details,
                          exprs_ns::exitCodeValue( cancelled ? exprs_ns::ExitCode::Cancelled
                                                             : exprs_ns::ExitCode::ExecutionFailure ),
                          error.toJson(), error.message() );
    }
    catch ( const std::exception &exception )
    {
        return io.finish( false, "run", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::ExecutionFailure ),
                          {}, std::string( "operator threw: " ) + exception.what() );
    }

    if ( result.isObject() && result.get( "success", true ).asBool() == false
         && result.isMember( "error" ) )
    {
        const Json::Value pluginDiagnostics =
            exprs_ns::PluginRegistry::instance().diagnostics().toJson();
        return io.finish( false, "run", result, exprs_ns::exitCodeValue( exprs_ns::ExitCode::ExecutionFailure ),
                          pluginDiagnostics,
                          result["error"].isString() ? result["error"].asString() : "execution failed" );
    }
    if ( sicnu::cli::cliIsInterrupted() )
    {
        return io.finish( false, "run", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::Cancelled ),
                          {}, "cancelled by signal" );
    }
    return io.finish( true, "run", result, 0 );
}

// ---------------------------------------------------------------------------
// pipeline
// ---------------------------------------------------------------------------
int commandPipeline( QStringList args, const CliIO &io )
{
    extractGlobalFlags( args );
    QString sub = args.isEmpty() ? "run" : args.takeFirst();
    if ( args.isEmpty() )
    {
        return io.finish( false, "pipeline", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "usage: pipeline run <file.json>" );
    }
    const QString path = args.takeFirst();

    auto progressCb = [&io]( int stepIndex, int totalSteps, double stepProgress,
                             const std::string &message ) {
        io.reportProgress( stepIndex, totalSteps, stepProgress, message );
    };
    auto logCb = [&io]( const std::string &level, const std::string &message ) {
        io.reportLog( level, message );
    };

    RsPipelineRunner runner( progressCb, logCb );
    RsPipelineRunner::PipelineResult result;
    if ( sub == "validate" )
    {
        std::string error;
        const Json::Value document = parseJsonFile( path.toStdString(), error );
        if ( !error.empty() )
        {
            return io.finish( false, "pipeline", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, error );
        }
        if ( !RsPipelineRunner::validatePipelineJson( document, &error ) )
        {
            return io.finish( false, "pipeline", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ),
                              {}, error );
        }
        Json::Value data( Json::objectValue );
        data["valid"] = true;
        return io.finish( true, "pipeline", data, 0 );
    }
    if ( sub == "resume" )
    {
        result = runner.resumeRun( path.toStdString() );
    }
    else
    {
        result = runner.runFromFile( path.toStdString() );
    }

    if ( !result.success )
    {
        return io.finish( false, "pipeline", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::ExecutionFailure ),
                          {}, result.errorMessage );
    }
    Json::Value steps( Json::arrayValue );
    for ( const auto &step : result.steps )
    {
        Json::Value entry( Json::objectValue );
        entry["operator"] = step.operatorName;
        entry["success"] = step.success;
        entry["result"] = step.result;
        steps.append( entry );
    }
    Json::Value data( Json::objectValue );
    data["steps"] = steps;
    return io.finish( true, "pipeline", data, 0 );
}

// ---------------------------------------------------------------------------
// workflow
// ---------------------------------------------------------------------------
int commandWorkflow( QStringList args, const CliIO &io )
{
    extractGlobalFlags( args );
    const QString sub = args.isEmpty() ? QString() : args.takeFirst();

    if ( sub == "list-runs" )
    {
        const QString dir = sicnu::workflow::WorkflowRunCoordinator::instance().checkpointDirectory();
        Json::Value runs( Json::arrayValue );
        const QStringList checkpoints = sicnu::workflow::WorkflowCheckpointManager().listCheckpoints( dir );
        for ( const QString &file : checkpoints )
        {
            QString loadError;
            auto run = sicnu::workflow::WorkflowCheckpointManager().loadCheckpoint( file, &loadError );
            if ( !run )
                continue;
            Json::Value entry( Json::objectValue );
            entry["run_id"] = run->runId();
            entry["state"] = sicnu::workflow::workflowRunStateToString( run->state() );
            entry["workflow"] = run->workflowId();
            entry["steps"] = static_cast<Json::Int>( run->stepPlans().size() );
            runs.append( entry );
        }
        return io.finish( true, "workflow", runs, 0 );
    }

    if ( sub == "resume" )
    {
        if ( args.isEmpty() )
        {
            return io.finish( false, "workflow", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "usage: workflow resume <run_id>" );
        }
        auto progressCb = [&io]( int stepIndex, int totalSteps, double stepProgress,
                                 const std::string &message ) {
            io.reportProgress( stepIndex, totalSteps, stepProgress, message );
        };
        auto logCb = [&io]( const std::string &level, const std::string &message ) {
            io.reportLog( level, message );
        };
        RsPipelineRunner runner( progressCb, logCb );
        const auto result = runner.resumeRun( args.takeFirst().toStdString() );
        if ( !result.success )
        {
            return io.finish( false, "workflow", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::ExecutionFailure ),
                              {}, result.errorMessage );
        }
        Json::Value data( Json::objectValue );
        data["steps"] = static_cast<Json::Int>( result.steps.size() );
        return io.finish( true, "workflow", data, 0 );
    }

    if ( sub == "validate" || sub == "run" )
    {
        if ( args.isEmpty() )
        {
            return io.finish( false, "workflow", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "usage: workflow " + sub.toStdString() + " <file.json>" );
        }
        const QString path = args.takeFirst();
        std::string error;
        const Json::Value document = parseJsonFile( path.toStdString(), error );
        if ( !error.empty() )
        {
            return io.finish( false, "workflow", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, error );
        }

        // Public schema validation (versioned contract).
        exprs_ns::PluginDiagnosticLog diagnostics;
        if ( !exprs_ns::validateWorkflowDocument( document, diagnostics ) )
        {
            return io.finish( false, "workflow", diagnostics.toJson(),
                              exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ),
                              diagnostics.toJson(), "workflow document failed schema validation" );
        }

        // Engine-level semantic validation (topology, ports, aliases).
        sicnu::workflow::WorkflowDefinition definition;
        if ( !sicnu::workflow::workflowDefinitionFromJson( document, definition, error ) )
        {
            return io.finish( false, "workflow", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ),
                              {}, "engine validation failed: " + error );
        }
        std::vector<std::string> ordered;
        if ( !sicnu::workflow::topologicalSortSteps( definition, ordered, error ) )
        {
            return io.finish( false, "workflow", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ),
                              {}, error );
        }
        if ( sub == "validate" )
        {
            Json::Value data( Json::objectValue );
            data["valid"] = true;
            data["schema_version"] = exprs_ns::kWorkflowSchemaVersion;
            data["steps"] = static_cast<Json::Int>( definition.steps.size() );
            return io.finish( true, "workflow", data, 0 );
        }

        // run: convert the public document to pipeline JSON and execute it
        // through the TaskCenter DAG (RsPipelineRunner).
        Json::Value pipeline( Json::objectValue );
        pipeline["name"] = document.get( "title", document.get( "id", "workflow" ).asString() ).asString();
        Json::Value steps( Json::arrayValue );
        for ( const Json::Value &step : document["steps"] )
        {
            Json::Value pipelineStep( Json::objectValue );
            pipelineStep["id"] = step.get( "id", "" ).asString();
            pipelineStep["operator"] = step.isMember( "operator" )
                                           ? step["operator"]
                                           : step.get( "operatorId", step.get( "name", "" ) );
            Json::Value params = step.get( "params", Json::Value( Json::objectValue ) );
            for ( const Json::Value &input : step["inputs"] )
            {
                if ( input.isObject() && input.isMember( "port" ) && input.isMember( "source" ) )
                    params[input["port"].asString()] = input["source"];
            }
            pipelineStep["params"] = params;
            steps.append( pipelineStep );
        }
        pipeline["steps"] = steps;

        auto progressCb = [&io]( int stepIndex, int totalSteps, double stepProgress,
                                 const std::string &message ) {
            io.reportProgress( stepIndex, totalSteps, stepProgress, message );
        };
        auto logCb = [&io]( const std::string &level, const std::string &message ) {
            io.reportLog( level, message );
        };
        RsPipelineRunner runner( progressCb, logCb );
        const auto result = runner.runFromJson( pipeline );
        if ( !result.success )
        {
            return io.finish( false, "workflow", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::ExecutionFailure ),
                              {}, result.errorMessage );
        }
        Json::Value data( Json::objectValue );
        data["workflow"] = document.get( "id", "" ).asString();
        data["steps"] = static_cast<Json::Int>( result.steps.size() );
        return io.finish( true, "workflow", data, 0 );
    }

    return io.finish( false, "workflow", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                      {}, "usage: workflow validate|run|list-runs|resume ..." );
}

// ---------------------------------------------------------------------------
// plugin
// ---------------------------------------------------------------------------
int commandPlugin( QStringList args, const CliIO &io )
{
    extractGlobalFlags( args );
    const QString sub = args.isEmpty() ? "list" : args.takeFirst();
    exprs_ns::PluginRegistry &registry = exprs_ns::PluginRegistry::instance();

    if ( sub == "list" )
    {
        Json::Value plugins( Json::arrayValue );
        for ( const exprs_ns::PluginRecord &record : registry.records() )
        {
            Json::Value entry( Json::objectValue );
            entry["id"] = record.manifest.id;
            entry["name"] = record.manifest.name;
            entry["version"] = record.manifest.version;
            entry["state"] = exprs_ns::pluginStateName( record.state );
            entry["origin"] = exprs_ns::pluginOriginName( record.origin );
            Json::Value caps( Json::arrayValue );
            for ( const std::string &capability : record.manifest.capabilities )
                caps.append( capability );
            entry["capabilities"] = caps;
            plugins.append( entry );
        }
        return io.finish( true, "plugin", plugins, 0 );
    }

    if ( sub == "validate" || sub == "doctor" )
    {
        if ( args.isEmpty() )
        {
            return io.finish( false, "plugin", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "usage: plugin validate|doctor <plugin-dir>" );
        }
        const std::string directory = args.takeFirst().toStdString();
        exprs_ns::PluginDiagnosticLog diagnostics;
        exprs_ns::PluginRecord record = exprs_ns::PluginDiscovery::inspectDirectory( directory, diagnostics );
        Json::Value data( Json::objectValue );
        data["directory"] = directory;
        data["state"] = exprs_ns::pluginStateName( record.state );
        data["valid"] = record.state == exprs_ns::PluginState::Validated;
        if ( sub == "doctor" && record.state == exprs_ns::PluginState::Validated
             && record.manifest.entrypointKind == exprs_ns::PluginEntrypointKind::Native
             && !record.manifest.entrypoint.empty() )
        {
            std::string probeError;
            const bool entrypointOk = exprs_ns::PluginLoader::probeEntrypoint(
                directory + "/" + record.manifest.entrypoint, probeError );
            data["entrypoint_symbol"] = entrypointOk;
            if ( !entrypointOk )
                data["entrypoint_error"] = probeError;
            data["api_version"] = record.manifest.apiVersion;
            data["abi_version"] = record.manifest.abiVersion;
            data["host_api_version"] = std::string( EXP_RS_PLUGIN_API_VERSION );
            data["host_abi_version"] = exprs_ns::pluginAbiVersion();
        }
        data["diagnostics"] = diagnostics.toJson();
        const bool ok = record.state == exprs_ns::PluginState::Validated
                        && !diagnostics.hasErrors();
        return io.finish( ok, "plugin", data,
                          ok ? 0 : exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ),
                          diagnostics.toJson(), ok ? "" : "plugin validation failed" );
    }

    if ( sub == "enable" || sub == "disable" )
    {
        if ( args.isEmpty() )
        {
            return io.finish( false, "plugin", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "usage: plugin " + sub.toStdString() + " <plugin-id>" );
        }
        const std::string pluginId = args.takeFirst().toStdString();
        if ( !registry.record( pluginId ) )
        {
            return io.finish( false, "plugin", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::MissingDependency ),
                              {}, "unknown plugin: " + pluginId );
        }
        const bool ok = registry.setEnabled( pluginId, sub == "enable" );
        Json::Value data( Json::objectValue );
        data["id"] = pluginId;
        data["enabled"] = sub == "enable";
        return io.finish( ok, "plugin", data, ok ? 0 : 1 );
    }

    if ( sub == "install" )
    {
        if ( args.isEmpty() )
        {
            return io.finish( false, "plugin", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "usage: plugin install <package-dir>" );
        }
        exprs_ns::PluginDiagnosticLog diagnostics;
        std::string installedDir;
        if ( !exprs_ns::PluginPackage::install( args.takeFirst().toStdString(), installedDir,
                                                diagnostics ) )
        {
            return io.finish( false, "plugin", diagnostics.toJson(),
                              exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ),
                              diagnostics.toJson(), "install failed" );
        }
        Json::Value data( Json::objectValue );
        data["installed_to"] = installedDir;
        return io.finish( true, "plugin", data, 0, diagnostics.toJson() );
    }

    if ( sub == "uninstall" )
    {
        if ( args.isEmpty() )
        {
            return io.finish( false, "plugin", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "usage: plugin uninstall <plugin-id>" );
        }
        exprs_ns::PluginDiagnosticLog diagnostics;
        if ( !exprs_ns::PluginPackage::uninstall( args.takeFirst().toStdString(), diagnostics ) )
        {
            return io.finish( false, "plugin", diagnostics.toJson(), 1, diagnostics.toJson(),
                              "uninstall failed" );
        }
        return io.finish( true, "plugin", {}, 0, diagnostics.toJson() );
    }

    if ( sub == "inspect" )
    {
        if ( args.isEmpty() )
        {
            return io.finish( false, "plugin", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "usage: plugin inspect <plugin-id>" );
        }
        const std::string pluginId = args.takeFirst().toStdString();
        const exprs_ns::PluginRecord *record = registry.record( pluginId );
        if ( !record )
        {
            return io.finish( false, "plugin", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::MissingDependency ),
                              {}, "unknown plugin: " + pluginId );
        }
        Json::Value data = record->toJson();
        return io.finish( true, "plugin", data, 0 );
    }

    return io.finish( false, "plugin", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                      {}, "usage: plugin list|validate|doctor|enable|disable|install|uninstall|inspect ..." );
}

// ---------------------------------------------------------------------------
// models / data-providers
// ---------------------------------------------------------------------------
int commandModels( QStringList args, const CliIO &io )
{
    extractGlobalFlags( args );
    const QString sub = args.isEmpty() ? "list" : args.takeFirst();
    const auto &catalog = operators::ModelCatalog::instance();
    const auto models = catalog.models();

    if ( sub == "inspect" && !args.isEmpty() )
    {
        const std::string name = args.takeFirst().toStdString();
        const auto model = catalog.find( name );
        if ( !model.has_value() )
        {
            return io.finish( false, "models", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::MissingDependency ),
                              {}, "unknown model: " + name );
        }
        Json::Value data = model->toJson();
        data["readiness"] = operators::modelReadinessName( model->readiness );
        return io.finish( true, "models", data, 0 );
    }

    Json::Value list( Json::arrayValue );
    for ( const auto &model : models )
    {
        Json::Value entry( Json::objectValue );
        entry["name"] = model.name;
        entry["task"] = model.task;
        entry["framework"] = model.framework;
        entry["gpu"] = model.gpu;
        entry["readiness"] = operators::modelReadinessName( model.readiness );
        list.append( entry );
    }
    return io.finish( true, "models", list, 0 );
}

int commandProject( QStringList args, const CliIO &io )
{
    extractGlobalFlags( args );
    const QString sub = args.isEmpty() ? "info" : args.takeFirst();
    if ( sub != "info" || args.isEmpty() )
    {
        return io.finish( false, "project", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "usage: project info <file.qgz|.qgs>" );
    }
    const QString path = args.takeFirst();
    if ( !QFileInfo::exists( path ) )
    {
        return io.finish( false, "project", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "project file not found: " + path.toStdString() );
    }
    QgsProject project;
    if ( !project.read( path ) )
    {
        return io.finish( false, "project", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "cannot read project: " + project.error().toStdString() );
    }
    Json::Value data( Json::objectValue );
    data["path"] = path.toStdString();
    data["title"] = project.title().toStdString();
    data["crs"] = project.crs().authid().toStdString();
    Json::Value layers( Json::arrayValue );
    const auto layerIds = project.mapLayers().keys();
    for ( const QString &layerId : layerIds )
    {
        const QgsMapLayer *layer = project.mapLayer( layerId );
        if ( !layer )
            continue;
        Json::Value entry( Json::objectValue );
        entry["id"] = layerId.toStdString();
        entry["name"] = layer->name().toStdString();
        entry["type"] = layer->type() == Qgis::LayerType::Raster
                            ? "raster"
                            : ( layer->type() == Qgis::LayerType::Vector ? "vector" : "other" );
        entry["source"] = layer->source().toStdString();
        entry["crs"] = layer->crs().authid().toStdString();
        layers.append( entry );
    }
    data["layers"] = layers;
    return io.finish( true, "project", data, 0 );
}

int commandDataProviders( QStringList args, const CliIO &io )
{
    extractGlobalFlags( args );
    const auto providers = sicnu::plugins::DataProviderRegistry::instance().providers();
    Json::Value list( Json::arrayValue );
    for ( const auto &provider : providers )
    {
        Json::Value entry( Json::objectValue );
        entry["id"] = provider.providerId;
        entry["plugin"] = provider.pluginId;
        entry["display_name"] = provider.displayName;
        entry["description"] = provider.description;
        Json::Value schemes( Json::arrayValue );
        for ( const std::string &scheme : provider.schemes )
            schemes.append( scheme );
        entry["schemes"] = schemes;
        list.append( entry );
    }
    return io.finish( true, "data-providers", list, 0 );
}

int commandCatalogExport( QStringList args, const CliIO &io )
{
    extractGlobalFlags( args );
    if ( args.isEmpty() )
    {
        return io.finish( false, "catalog", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "usage: catalog export <dir>" );
    }
    const QString outDir = args.takeFirst();
    const auto descriptors = processing::AtomicAlgorithmRegistry::instance().listDescriptors();
    std::string error;
    const int written = sicnu::processing::AlgorithmMetaStore::exportCatalog(
        outDir.toStdString(), descriptors, &error );
    if ( written < 0 )
    {
        return io.finish( false, "catalog", {}, 1, {}, "failed to export catalog: " + error );
    }
    Json::Value data( Json::objectValue );
    data["written"] = written;
    data["directory"] = outDir.toStdString();
    return io.finish( true, "catalog", data, 0 );
}

} // namespace

namespace sicnu::cli {

// cliIsInterrupted() is defined in rs_pipeline_runner.cpp (#455) alongside
// the g_cliInterrupted flag; declared in cli_commands.h.

void CliIO::reportProgress( int stepIndex, int totalSteps, double stepProgress,
                            const std::string &message ) const
{
    if ( quiet )
        return;
    const int percent = static_cast<int>( stepProgress * 100.0 );
    if ( progressJson || json || jsonLines )
    {
        Json::Value record( Json::objectValue );
        record["type"] = "progress";
        record["step"] = stepIndex;
        record["steps"] = totalSteps;
        record["percent"] = percent;
        record["message"] = message;
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::cerr << Json::writeString( builder, record ) << "\n";
        return;
    }
    std::cerr << "[Step " << stepIndex << "/" << totalSteps << " " << percent << "%] " << message
              << "\n";
}

void CliIO::reportLog( const std::string &level, const std::string &message ) const
{
    if ( quiet )
        return;
    if ( json || jsonLines || progressJson )
    {
        Json::Value record( Json::objectValue );
        record["type"] = "log";
        record["level"] = level;
        record["message"] = message;
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::cerr << Json::writeString( builder, record ) << "\n";
        return;
    }
    std::cerr << "[" << level << "] " << message << "\n";
}

int CliIO::finish( bool ok, const std::string &command, Json::Value data, int exitCode,
                   const Json::Value &diagnostics, const std::string &errorMessage ) const
{
    if ( json || jsonLines )
    {
        Json::Value envelope( Json::objectValue );
        envelope["ok"] = ok;
        envelope["command"] = command;
        if ( !errorMessage.empty() )
            envelope["error"] = errorMessage;
        envelope["data"] = data;
        if ( !diagnostics.isNull() )
            envelope["diagnostics"] = diagnostics;
        envelope["api_version"] = std::string( EXP_RS_PLUGIN_API_VERSION );
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::cout << Json::writeString( builder, envelope ) << "\n";
    }
    else if ( !ok && !errorMessage.empty() )
    {
        std::cerr << errorMessage << "\n";
    }
    return exitCode;
}

bool isCliCommand( const QString &firstArg )
{
    static const QStringList kCommands = { "algorithms", "run", "pipeline", "workflow", "plugin",
                                           "models", "catalog", "project", "data-providers" };
    return kCommands.contains( firstArg );
}

int dispatchCliCommand( const QStringList &arguments, const CliIO &io )
{
    if ( arguments.isEmpty() )
        return exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput );

    QStringList args = arguments;
    const QString command = args.takeFirst();

    if ( command == "algorithms" )
        return commandAlgorithms( std::move( args ), io );
    if ( command == "run" )
        return commandRun( std::move( args ), io );
    if ( command == "pipeline" )
        return commandPipeline( std::move( args ), io );
    if ( command == "workflow" )
        return commandWorkflow( std::move( args ), io );
    if ( command == "plugin" )
        return commandPlugin( std::move( args ), io );
    if ( command == "models" )
        return commandModels( std::move( args ), io );
    if ( command == "project" )
        return commandProject( std::move( args ), io );
    if ( command == "data-providers" )
        return commandDataProviders( std::move( args ), io );
    if ( command == "catalog" )
    {
        // catalog export <dir> — the legacy --export-catalog surface.
        const QString sub = args.isEmpty() ? "export" : args.takeFirst();
        if ( sub != "export" || args.isEmpty() )
            return io.finish( false, "catalog", {},
                              exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ), {},
                              "usage: catalog export <dir>" );
        return commandCatalogExport( args, io );
    }
    return exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput );
}

} // namespace sicnu::cli
