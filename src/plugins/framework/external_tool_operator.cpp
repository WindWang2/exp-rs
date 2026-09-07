/***************************************************************************
 * src/plugins/framework/external_tool_operator.cpp
 ***************************************************************************/
#include "external_tool_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_error.h"
#include "exprs/external_process.h"
#include "exprs/path_policy.h"

#include <filesystem>
#include <fstream>

namespace sicnu::plugins {

namespace {

std::string valueToString( const Json::Value &value )
{
    if ( value.isNull() )
        return {};
    if ( value.isString() )
        return value.asString();
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString( builder, value );
}

/// Substitutes ${name} placeholders from params. Unknown placeholders are
/// an error (fail closed rather than passing raw shell-ish text through).
bool substitute( std::string text, const Json::Value &params,
                 const std::map<std::string, std::string> &overrides, std::string &out,
                 std::string &error )
{
    std::string result;
    size_t position = 0;
    while ( position < text.size() )
    {
        const size_t begin = text.find( "${", position );
        if ( begin == std::string::npos )
        {
            result += text.substr( position );
            break;
        }
        const size_t end = text.find( '}', begin );
        if ( end == std::string::npos )
        {
            error = "unterminated placeholder in '" + text + "'";
            return false;
        }
        result += text.substr( position, begin - position );
        const std::string name = text.substr( begin + 2, end - begin - 2 );

        auto overrideEntry = overrides.find( name );
        if ( overrideEntry != overrides.end() )
        {
            result += overrideEntry->second;
        }
        else if ( params.isMember( name ) )
        {
            result += valueToString( params[name] );
        }
        else
        {
            error = "placeholder ${" + name + "} has no matching parameter";
            return false;
        }
        position = end + 1;
    }
    out = result;
    return true;
}

void ensureParentDirectory( const std::string &path )
{
    const size_t slash = path.rfind( '/' );
    if ( slash == std::string::npos )
        return;
    const std::string parent = path.substr( 0, slash );
    if ( parent.empty() )
        return;
    std::error_code error;
    std::filesystem::create_directories( parent, error );
}

} // namespace

ExternalToolOperator::ExternalToolOperator( std::string operatorId,
                                            exprs::ManifestOperator declaration,
                                            std::string pluginDir )
    : mOperatorId( std::move( operatorId ) )
    , mDeclaration( std::move( declaration ) )
    , mPluginDir( std::move( pluginDir ) )
{
}

bool ExternalToolOperator::buildArgv( const Json::Value &params,
                                      sicnu::operators::RSOperatorContext &context,
                                      std::vector<std::string> &argv,
                                      std::map<std::string, std::pair<std::string, std::string>> &outputMoves,
                                      std::string &error ) const
{
    // Declared outputs are redirected to temp files and published (renamed)
    // only after a clean exit — the transactional publish contract.
    std::map<std::string, std::string> overrides;
    if ( !mPluginDir.empty() )
        overrides["plugin_dir"] = mPluginDir;
    for ( const exprs::ManifestPort &port : mDeclaration.outputs )
    {
        const std::string finalPath =
            params.isMember( port.name ) ? valueToString( params[port.name] ) : std::string();
        if ( finalPath.empty() )
        {
            error = "output port '" + port.name + "' has no path parameter";
            return false;
        }
        std::string suffix;
        const size_t dot = finalPath.rfind( '.' );
        if ( dot != std::string::npos && finalPath.find( '/', dot ) == std::string::npos )
            suffix = finalPath.substr( dot );
        const std::string tempPath = context.tempPath( suffix );
        overrides[port.name] = tempPath;
        outputMoves[port.name] = { tempPath, finalPath };
    }

    for ( const std::string &raw : mDeclaration.external.argv )
    {
        std::string substituted;
        if ( !substitute( raw, params, overrides, substituted, error ) )
            return false;
        argv.push_back( substituted );
    }
    return true;
}

Json::Value ExternalToolOperator::run( const Json::Value &params,
                                       sicnu::operators::RSOperatorContext &context )
{
    context.throwIfCancelled();

    if ( !mDeclaration.hasExternalTool )
    {
        throw sicnu::operators::RSOperatorError(
            sicnu::operators::ErrorCode::NotInitialized, "operator has no external tool section" );
    }

    std::vector<std::string> argv;
    std::map<std::string, std::pair<std::string, std::string>> outputMoves;
    std::string error;
    if ( !buildArgv( params, context, argv, outputMoves, error ) )
    {
        throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::InvalidParameter,
                                                 std::move( error ) );
    }

    exprs::ExternalProcessRequest request;
    request.argv = argv;
    request.environment = mDeclaration.external.environment;
    request.inheritEnvironment = mDeclaration.external.inheritEnvironment;
    request.timeoutSeconds = mDeclaration.external.timeoutSeconds;
    request.stdoutLimitBytes = mDeclaration.external.stdoutLimitBytes;
    request.stderrLimitBytes = mDeclaration.external.stderrLimitBytes;
    request.isCancelled = [&context]() { return context.isCancelled(); };
    // The plugin's own directory is a second containment root: external
    // tools legitimately read bundled payloads via ${plugin_dir} (#757).
    if ( !mPluginDir.empty() )
        request.additionalAllowedRoots.push_back( mPluginDir );
    // Declared outputs are redirected into the operator context's temp work
    // directory and later published to the final path — the child writes
    // there legitimately, so it is a containment root too (P1 review fix).
    request.additionalAllowedRoots.push_back( context.workDir() );
    if ( !mDeclaration.external.workingDirectoryParam.empty() )
    {
        // Accepts an absolute path or the name of a params key holding one.
        const std::string &spec = mDeclaration.external.workingDirectoryParam;
        std::string directory;
        if ( params.isMember( spec ) )
            directory = valueToString( params[spec] );
        else
            directory = spec;
        if ( !directory.empty() )
            request.workingDirectory = directory;
    }

    context.reportProgress( 0.05, "starting " + argv.front() );

    exprs::ExternalProcessResult processResult = exprs::ExternalProcess::run( request );

    if ( !processResult.stdErr.empty() )
        context.logInfo( processResult.stdErr.substr( 0, 2000 ) );

    if ( processResult.cancelled )
    {
        throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled,
                                                 processResult.error );
    }
    if ( processResult.refusedByPolicy )
    {
        // Workspace effect policy refused the spawn before any process
        // existed (issue #757). Nothing ran, nothing was published.
        throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::ExternalProcessFailed,
                                                 processResult.error );
    }
    if ( processResult.timedOut )
    {
        throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::ExternalProcessTimeout,
                                                 processResult.error );
    }
    if ( !processResult.exitedCleanly() )
    {
        // Failed runs leave no partially-published outputs behind.
        std::string detail = "exit code " + std::to_string( processResult.exitCode );
        if ( processResult.exitSignal )
            detail += " (signal " + std::to_string( processResult.exitSignal ) + ")";
        if ( !processResult.started )
            detail = processResult.error;
        else if ( !processResult.stdErr.empty() )
            detail += ": " + processResult.stdErr.substr( 0, 1000 );
        throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::ExternalProcessFailed,
                                                 "external tool failed (" + detail + ")" );
    }

    // Transactional publish: temp -> final for every declared output.
    // Output targets come from parameters and are written by the host (not
    // the child), so the workspace effect policy gates them here too (#757).
    Json::Value published( Json::objectValue );
    for ( const auto &entry : outputMoves )
    {
        const std::string &tempPath = entry.second.first;
        const std::string &finalPath = entry.second.second;
        std::error_code ec;
        if ( !std::filesystem::exists( tempPath, ec ) )
        {
            throw sicnu::operators::RSOperatorError(
                sicnu::operators::ErrorCode::FileNotWritable,
                "external tool did not produce declared output '" + entry.first + "'" );
        }
        if ( !exprs::PathPolicy::workspaceRoot().empty() )
        {
            bool contained = exprs::PathPolicy::resolvesInsideRoot(
                exprs::PathPolicy::workspaceRoot(), finalPath );
            if ( !contained && !mPluginDir.empty() )
                contained = exprs::PathPolicy::resolvesInsideRoot( mPluginDir, finalPath );
            if ( !contained )
            {
                std::filesystem::remove( tempPath, ec );
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::FileNotWritable,
                    "output path escapes the workspace policy: " + finalPath );
            }
        }
        ensureParentDirectory( finalPath );
        // Copy+remove instead of rename when crossing filesystems.
        std::error_code renameError;
        std::filesystem::rename( tempPath, finalPath, renameError );
        if ( renameError )
        {
            std::error_code copyError;
            std::filesystem::copy_file( tempPath, finalPath,
                                        std::filesystem::copy_options::overwrite_existing,
                                        copyError );
            std::filesystem::remove( tempPath, copyError );
            if ( copyError )
            {
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::FileNotWritable,
                    "cannot publish output '" + finalPath + "': " + copyError.message() );
            }
        }
        published[entry.first] = finalPath;
    }

    context.reportProgress( 1.0, "completed" );

    Json::Value result( Json::objectValue );
    result["success"] = true;
    result["operator"] = mOperatorId;
    result["exit_code"] = processResult.exitCode;
    for ( const std::string &key : published.getMemberNames() )
        result[key] = published[key];
    if ( !published.empty() )
        result["output"] = *published.begin();
    if ( published.empty() && !processResult.stdOut.empty() )
    {
        // Pure-manifest tools report through stdout; surface it (bounded).
        result["stdout"] = processResult.stdOut.substr( 0, 65536 );
    }
    if ( processResult.truncatedStdout )
        result["stdout_truncated"] = true;
    if ( processResult.truncatedStderr )
        result["stderr_truncated"] = true;
    result["duration_ms"] = static_cast<Json::Int64>( processResult.durationMs );
    return result;
}

} // namespace sicnu::plugins
