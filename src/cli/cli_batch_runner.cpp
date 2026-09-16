// src/cli/cli_batch_runner.cpp — batch manifest runner (Surface-11 WP-B)

#include "cli_batch_runner.h"

#include "agent/tool_catalog/surface_redaction.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

#include <filesystem>

namespace sicnu::cli::batch {

namespace {

using sicnu::agent::tool_catalog::redaction::redactText;

namespace {
/// Single redaction boundary: every TaskRecord::error passes through here so
/// the result index AND the CLI envelope carry only redacted text.
void setError( TaskRecord &record, std::string message )
{
    record.error = redactText( std::move( message ) );
}
} // namespace

bool validTaskId( const std::string &id )
{
    if ( id.empty() || id.size() > 64 )
        return false;
    for ( const char c : id )
    {
        const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
            || ( c >= '0' && c <= '9' ) || c == '-' || c == '_' || c == '.';
        if ( !ok )
            return false;
    }
    return true;
}

/// ${name} interpolation over every string in the params tree. Missing
/// variables are reported via @p missing (first occurrence) and left as-is.
void interpolate( Json::Value &node, const std::map<std::string, std::string> &variables,
                  std::string *missing )
{
    if ( node.isString() )
    {
        const std::string text = node.asString();
        const auto begin = text.find( "${" );
        if ( begin == std::string::npos )
            return;
        std::string result;
        size_t pos = 0;
        while ( pos < text.size() )
        {
            const auto open = text.find( "${", pos );
            if ( open == std::string::npos )
            {
                result += text.substr( pos );
                break;
            }
            const auto close = text.find( '}', open );
            if ( close == std::string::npos )
            {
                result += text.substr( pos );
                break;
            }
            result += text.substr( pos, open - pos );
            const std::string name = text.substr( open + 2, close - open - 2 );
            const auto it = variables.find( name );
            if ( it == variables.end() )
            {
                if ( missing && missing->empty() )
                    *missing = name;
                result += text.substr( open, close - open + 1 );
            }
            else
            {
                result += it->second;
            }
            pos = close + 1;
        }
        node = Json::Value( result );
    }
    else if ( node.isObject() )
    {
        for ( const std::string &key : node.getMemberNames() )
            interpolate( node[key], variables, missing );
    }
    else if ( node.isArray() )
    {
        for ( Json::ArrayIndex i = 0; i < node.size(); ++i )
            interpolate( node[i], variables, missing );
    }
}

void writeResultIndex( const std::string &path, const std::vector<TaskRecord> &records,
                       const Callbacks &callbacks )
{
    if ( path.empty() )
        return;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out( tmp, std::ios::binary | std::ios::trunc );
        if ( !out )
        {
            if ( callbacks.log )
                callbacks.log( "error", "cannot write result index: " + tmp );
            return;
        }
        for ( const TaskRecord &record : records )
        {
            Json::Value line( Json::objectValue );
            line["index"] = record.index;
            line["id"] = record.id;
            // The operator id is caller input echoed back — through the same
            // redaction boundary as error text (a crafted id can carry a
            // credential shape; legit ids pass through unchanged).
            line["operator"] = redactText( record.operatorId );
            line["status"] = record.status;
            line["exit_code"] = record.exitCode;
            if ( !record.error.empty() )
                line["error"] = record.error; // redacted at setError()
            line["duration_ms"] = static_cast<Json::Int64>( record.durationMs );
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            out << Json::writeString( builder, line ) << "\n";
        }
    }
    // Atomic swap: readers never observe a partial index.
    std::error_code ec;
    std::filesystem::rename( tmp, path, ec );
    if ( ec )
    {
        std::filesystem::remove( tmp, ec );
        if ( callbacks.log )
            callbacks.log( "error", "cannot publish result index " + path + ": " + ec.message() );
    }
}

int aggregateExitCode( const std::vector<TaskRecord> &records, bool cancelled )
{
    if ( cancelled )
        return 4; // exprs::ExitCode::Cancelled
    int worst = 0;
    for ( const TaskRecord &record : records )
        worst = std::max( worst, record.exitCode );
    return worst;
}

} // namespace

bool parseManifestDocument( const Json::Value &document, Json::Value &normalized, std::string *error )
{
    auto fail = [error]( const std::string &message ) {
        if ( error )
            *error = message;
        return false;
    };

    if ( !document.isObject() && !document.isArray() )
        return fail( "manifest must be a JSON object (or an array of task objects)" );

    Json::Value root = document.isObject() ? document : Json::Value();
    Json::Value tasks( Json::arrayValue );

    if ( document.isArray() )
    {
        tasks = document;
    }
    else if ( root.isMember( "tasks" ) )
    {
        // Strict root contract: a typo'd top-level key ({"polcy": …}) must
        // not silently drop its payload.
        for ( const std::string &key : root.getMemberNames() )
        {
            if ( key != "version" && key != "variables" && key != "policy" && key != "tasks" )
                return fail( "unknown manifest key: " + key );
        }
        if ( !root["tasks"].isArray() )
            return fail( "manifest 'tasks' must be an array" );
        tasks = root["tasks"];
        if ( root.isMember( "version" )
             && ( !root["version"].isIntegral() || root["version"].asInt() != 1 ) )
            return fail( "unsupported manifest version (expected 1)" );
    }
    else
    {
        return fail( "manifest object requires a 'tasks' array" );
    }

    // Policy: continue (default) | fail-fast. Unknown keys are contract
    // violations — a typo'd "on_error" must not silently keep the default.
    Json::Value policy( Json::objectValue );
    policy["on_error"] = "continue";
    if ( document.isObject() && root.isMember( "policy" ) )
    {
        const Json::Value &inPolicy = root["policy"];
        if ( !inPolicy.isObject() )
            return fail( "manifest 'policy' must be an object" );
        for ( const std::string &key : inPolicy.getMemberNames() )
        {
            if ( key != "on_error" )
                return fail( "unknown policy key: " + key );
        }
        if ( inPolicy.isMember( "on_error" ) )
        {
            if ( !inPolicy["on_error"].isString() )
                return fail( "policy.on_error must be a string" );
            const std::string on = inPolicy["on_error"].asString();
            if ( on != "continue" && on != "fail-fast" )
                return fail( "policy.on_error must be 'continue' or 'fail-fast'" );
            policy["on_error"] = on;
        }
    }

    // Variables: scalars only, stringified for interpolation.
    Json::Value variables( Json::objectValue );
    if ( document.isObject() && root.isMember( "variables" ) )
    {
        const Json::Value &inVariables = root["variables"];
        if ( !inVariables.isObject() )
            return fail( "manifest 'variables' must be an object" );
        for ( const std::string &key : inVariables.getMemberNames() )
        {
            const Json::Value &value = inVariables[key];
            if ( value.isString() )
                variables[key] = value;
            else if ( value.isIntegral() )
                variables[key] = std::to_string( value.asInt64() );
            else if ( value.isNumeric() )
                variables[key] = std::to_string( value.asDouble() );
            else if ( value.isBool() )
                variables[key] = value.asBool() ? "true" : "false";
            else
                return fail( "variable '" + key + "' must be a scalar" );
        }
    }

    std::vector<std::string> seenIds;
    for ( Json::ArrayIndex i = 0; i < tasks.size(); ++i )
    {
        const Json::Value &task = tasks[i];
        if ( !task.isObject() )
            return fail( "task #" + std::to_string( i ) + " must be an object" );
        for ( const std::string &key : task.getMemberNames() )
        {
            if ( key != "id" && key != "operator" && key != "params" && key != "params_file"
                 && key != "enabled" )
                return fail( "task #" + std::to_string( i ) + " has unknown key: " + key );
        }
        if ( !task.isMember( "id" ) || !task["id"].isString() || !validTaskId( task["id"].asString() ) )
            return fail( "task #" + std::to_string( i ) + " needs an id matching [-A-Za-z0-9_.]{1,64}" );
        const std::string id = task["id"].asString();
        if ( std::find( seenIds.begin(), seenIds.end(), id ) != seenIds.end() )
            return fail( "duplicate task id: " + id );
        seenIds.push_back( id );
        if ( !task.isMember( "operator" ) || !task["operator"].isString()
             || task["operator"].asString().empty() )
            return fail( "task '" + id + "' needs a non-empty 'operator'" );
        if ( task.isMember( "params" ) && !task["params"].isObject() )
            return fail( "task '" + id + "' params must be an object" );
        if ( task.isMember( "params_file" )
             && ( !task["params_file"].isString() || task["params_file"].asString().empty() ) )
            return fail( "task '" + id + "' params_file must be a path string" );
        if ( task.isMember( "enabled" ) && !task["enabled"].isBool() )
            return fail( "task '" + id + "' enabled must be a boolean" );
    }

    normalized = Json::Value( Json::objectValue );
    normalized["version"] = 1;
    normalized["variables"] = variables;
    normalized["policy"] = policy;
    normalized["tasks"] = tasks;
    return true;
}

bool loadManifestFile( const std::string &path, Json::Value &normalized, std::string *error )
{
    std::ifstream input( path, std::ios::binary );
    if ( !input )
    {
        if ( error )
            *error = "cannot open manifest: " + path;
        return false;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();

    const bool jsonl = path.size() >= 6 && path.rfind( ".jsonl" ) == path.size() - 6;
    Json::Value document;
    if ( jsonl )
    {
        document = Json::Value( Json::arrayValue );
        std::istringstream lines( text );
        std::string line;
        while ( std::getline( lines, line ) )
        {
            const auto first = line.find_first_not_of( " \t\r\n" );
            if ( first == std::string::npos || line[first] == '#' )
                continue;
            Json::Value parsed;
            Json::Reader reader;
            if ( !reader.parse( line, parsed, false ) )
            {
                if ( error )
                    *error = "invalid JSONL line in " + path + ": " + reader.getFormattedErrorMessages();
                return false;
            }
            document.append( parsed );
        }
        if ( document.empty() )
        {
            if ( error )
                *error = "manifest contains no tasks: " + path;
            return false;
        }
    }
    else
    {
        Json::Reader reader;
        if ( !reader.parse( text, document, false ) )
        {
            if ( error )
                *error = "invalid JSON in " + path + ": " + reader.getFormattedErrorMessages();
            return false;
        }
    }

    // JSONL header line: an object with variables/policy but no id/operator.
    if ( jsonl && document.isArray() && document[0].isObject() && !document[0].isMember( "id" )
         && !document[0].isMember( "operator" )
         && ( document[0].isMember( "variables" ) || document[0].isMember( "policy" ) ) )
    {
        Json::Value header = document[0];
        Json::Value rest( Json::arrayValue );
        for ( Json::ArrayIndex i = 1; i < document.size(); ++i )
            rest.append( document[i] );
        header["tasks"] = rest;
        document = header;
    }

    return parseManifestDocument( document, normalized, error );
}

Outcome runManifest( const Json::Value &normalized, const Options &options,
                     const Callbacks &callbacks )
{
    Outcome outcome;
    const Json::Value &tasks = normalized["tasks"];

    std::map<std::string, std::string> variables;
    for ( const std::string &key : normalized["variables"].getMemberNames() )
        variables[key] = normalized["variables"][key].asString();
    for ( const auto &pair : options.extraVariables )
        variables[pair.first] = pair.second;

    const bool failFast = options.failFast
        || normalized["policy"]["on_error"].asString() == "fail-fast";
    const int total = static_cast<int>( tasks.size() );
    bool stop = false;

    for ( Json::ArrayIndex i = 0; i < tasks.size() && !stop; ++i )
    {
        const Json::Value &task = tasks[i];
        TaskRecord record;
        record.index = static_cast<int>( i );
        record.id = task["id"].asString();
        record.operatorId = task["operator"].asString();

        if ( task.isMember( "enabled" ) && !task["enabled"].asBool() )
        {
            record.status = "skipped";
            record.error = "disabled";
            outcome.records.push_back( record );
            continue;
        }

        if ( callbacks.isCancelled && callbacks.isCancelled() )
        {
            outcome.cancelled = true;
            record.status = "cancelled";
            record.error = "cancelled before start";
            outcome.records.push_back( record );
            for ( Json::ArrayIndex j = i + 1; j < tasks.size(); ++j )
            {
                TaskRecord skipped;
                skipped.index = static_cast<int>( j );
                skipped.id = tasks[j]["id"].asString();
                skipped.operatorId = tasks[j]["operator"].asString();
                skipped.status = "skipped";
                skipped.error = "skipped: cancelled run";
                outcome.records.push_back( skipped );
            }
            break;
        }

        // Params: file first, inline overrides, then ${var} interpolation.
        Json::Value params( Json::objectValue );
        if ( task.isMember( "params_file" ) )
        {
            std::ifstream in( task["params_file"].asString(), std::ios::binary );
            if ( !in )
            {
                record.status = "failed";
                record.exitCode = 6; // InvalidInput
                setError( record, "cannot open params_file: " + task["params_file"].asString() );
                outcome.records.push_back( record );
                if ( failFast )
                    stop = true;
                continue;
            }
            std::stringstream buffer;
            buffer << in.rdbuf();
            Json::Reader reader;
            if ( !reader.parse( buffer.str(), params, false ) || !params.isObject() )
            {
                record.status = "failed";
                record.exitCode = 6;
                setError( record, "params_file is not a JSON object: " + task["params_file"].asString() );
                outcome.records.push_back( record );
                if ( failFast )
                    stop = true;
                continue;
            }
        }
        if ( task.isMember( "params" ) )
        {
            const Json::Value &inlineParams = task["params"];
            for ( const std::string &key : inlineParams.getMemberNames() )
                params[key] = inlineParams[key];
        }
        std::string missingVariable;
        interpolate( params, variables, &missingVariable );
        if ( !missingVariable.empty() )
        {
            record.status = "failed";
            record.exitCode = 6; // InvalidInput
            setError( record, "unknown variable ${" + missingVariable + "}" );
            outcome.records.push_back( record );
            if ( failFast )
                stop = true;
            continue;
        }

        auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
            record.operatorId );
        if ( !adapter )
        {
            record.status = "failed";
            record.exitCode = 5; // MissingDependency
            setError( record, "unknown operator: " + record.operatorId );
            outcome.records.push_back( record );
            if ( failFast )
                stop = true;
            continue;
        }

        if ( options.dryRun )
        {
            record.status = "ok";
            record.error = "dry-run";
            outcome.records.push_back( record );
            continue;
        }

        const auto started = std::chrono::steady_clock::now();
        try
        {
            Json::Value result = adapter->execute(
                params,
                [&]( int percent, const std::string &message ) {
                    if ( callbacks.progress )
                        callbacks.progress( record.index + 1, total, percent / 100.0,
                                            "[" + record.id + "] " + message );
                },
                [&callbacks]() { return callbacks.isCancelled ? callbacks.isCancelled() : false; } );
            if ( result.isObject() && result.get( "success", true ).asBool() == false
                 && result.isMember( "error" ) )
            {
                record.status = "failed";
                record.exitCode = 3; // ExecutionFailure
                setError( record, result["error"].isString() ? result["error"].asString()
                                                             : "execution failed" );
            }
            else if ( callbacks.isCancelled && callbacks.isCancelled() )
            {
                outcome.cancelled = true;
                record.status = "cancelled";
                setError( record, "cancelled after execute" );
            }
            else
            {
                record.status = "ok";
            }
        }
        catch ( const sicnu::operators::RSOperatorError &error )
        {
            if ( error.code() == sicnu::operators::ErrorCode::Cancelled )
            {
                outcome.cancelled = true;
                record.status = "cancelled";
                setError( record, error.message() );
            }
            else
            {
                record.status = "failed";
                record.exitCode = 3; // ExecutionFailure
                setError( record, error.message() );
            }
        }
        catch ( const std::exception &error )
        {
            record.status = "failed";
            record.exitCode = 3; // ExecutionFailure
            setError( record, std::string( "operator threw: " ) + error.what() );
        }
        record.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started )
                                .count();
        outcome.records.push_back( record );

        if ( record.status == "cancelled" )
        {
            for ( Json::ArrayIndex j = i + 1; j < tasks.size(); ++j )
            {
                TaskRecord skipped;
                skipped.index = static_cast<int>( j );
                skipped.id = tasks[j]["id"].asString();
                skipped.operatorId = tasks[j]["operator"].asString();
                skipped.status = "skipped";
                skipped.error = "skipped: cancelled run";
                outcome.records.push_back( skipped );
            }
            break;
        }
        if ( record.status == "failed" && failFast )
            stop = true;
    }

    // Tail tasks after a fail-fast stop are recorded as skipped.
    if ( stop )
    {
        const int recorded = static_cast<int>( outcome.records.size() );
        for ( int j = recorded; j < total; ++j )
        {
            TaskRecord skipped;
            skipped.index = j;
            skipped.id = tasks[j]["id"].asString();
            skipped.operatorId = tasks[j]["operator"].asString();
            skipped.status = "skipped";
            skipped.error = "skipped: fail-fast after failed task";
            outcome.records.push_back( skipped );
        }
    }

    outcome.exitCode = aggregateExitCode( outcome.records, outcome.cancelled );
    writeResultIndex( options.resultIndexPath, outcome.records, callbacks );
    return outcome;
}

Outcome runManifestFile( const std::string &path, const Options &options,
                         const Callbacks &callbacks, std::string *error )
{
    Json::Value normalized;
    if ( !loadManifestFile( path, normalized, error ) )
        return Outcome{};
    return runManifest( normalized, options, callbacks );
}

} // namespace sicnu::cli::batch
