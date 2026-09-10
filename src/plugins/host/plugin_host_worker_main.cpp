/***************************************************************************
 * src/plugins/host/plugin_host_worker_main.cpp — exprs_plugin_host_worker
 *
 * The out-of-process host for ONE native plugin (isolation runtime 5.0).
 * The worker maps the plugin binary in ITS OWN process through the standard
 * exprs::PluginLoader (V1 entry point — no new ABI), drives the lifecycle
 * and answers requests over the versioned IPC contract. A plugin crash
 * kills the worker, never the ExpRS host process; the launcher applies its
 * restart policy.
 *
 * Protocol handles arrive as inherited OS handles in argv (never stdio —
 * plugin printf noise lands on the worker's real stdout, a sink, and
 * cannot corrupt framing). Startup sequence:
 *   1. parse --exprs-ipc-read/--exprs-ipc-write
 *   2. send worker.hello (protocol/API/ABI axes) BEFORE any plugin code
 *   3. serve: plugin.load → contribution registration → execute requests
 *   4. plugin.shutdown → PluginV1::shutdown + dlclose → exit 0
 ***************************************************************************/
#include "exprs/host_protocol.h"
#include "exprs/ipc_channel.h"
#include "exprs/ipc_envelope.h"
#include "exprs/ipc_frame.h"
#include "exprs/ipc_stream.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_manifest.h"
#include "exprs/version.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "plugins/host/plugin_host_protocol.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace exprs;
using namespace sicnu::plugins::hostprotocol;

namespace {

/// Contribution sink of the loaded plugin (worker-local). Registrations are
/// validated against the manifest exactly like the in-process host does:
/// manifest-declared ids without a registration are reported at load time.
class WorkerSink : public PluginContributionSink
{
public:
    explicit WorkerSink( std::string pluginId = {} )
        : mPluginId( std::move( pluginId ) )
    {
    }

    bool declaredOperator( const std::string &id ) const
    {
        for ( const ManifestOperator &op : mManifest->operators )
            if ( op.id == id )
                return true;
        return false;
    }
    bool declaredDataProvider( const std::string &id ) const
    {
        for ( const ManifestDataProvider &provider : mManifest->dataProviders )
            if ( provider.id == id )
                return true;
        return false;
    }
    bool declaredModelRuntime( const std::string &framework ) const
    {
        for ( const ManifestModelRuntime &runtime : mManifest->modelRuntimes )
            if ( runtime.framework == framework )
                return true;
        return false;
    }
    bool declaredAgentTool( const std::string &id ) const
    {
        for ( const ManifestAgentTool &tool : mManifest->agentTools )
            if ( tool.id == id )
                return true;
        return false;
    }

    bool registerOperatorFactory(
        const std::string &, const std::string &operatorId,
        std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory ) override
    {
        if ( !declaredOperator( operatorId ) )
            return false;
        return mOperatorFactories.emplace( operatorId, std::move( factory ) ).second;
    }
    bool registerDataProvider( const std::string &, const std::string &providerId,
                               std::shared_ptr<IPluginDataProviderV1> provider ) override
    {
        if ( !declaredDataProvider( providerId ) )
            return false;
        return mDataProviders.emplace( providerId, std::move( provider ) ).second;
    }
    bool registerModelRuntime( const std::string &, const std::string &framework,
                               PluginModelRuntimeFactoryV1 factory ) override
    {
        if ( !declaredModelRuntime( framework ) )
            return false;
        return mModelRuntimeFactories.emplace( framework, std::move( factory ) ).second;
    }
    bool registerAgentTool( const std::string &, const std::string &toolId,
                            std::shared_ptr<IPluginAgentToolV1> tool ) override
    {
        if ( !declaredAgentTool( toolId ) )
            return false;
        return mAgentTools.emplace( toolId, std::move( tool ) ).second;
    }

    void setManifest( const PluginManifest *manifest ) { mManifest = manifest; }

    std::map<std::string, std::function<std::unique_ptr<sicnu::operators::RSOperator>()>>
        mOperatorFactories;
    std::map<std::string, std::shared_ptr<IPluginDataProviderV1>> mDataProviders;
    std::map<std::string, PluginModelRuntimeFactoryV1> mModelRuntimeFactories;
    std::map<std::string, std::shared_ptr<IPluginAgentToolV1>> mAgentTools;

private:
    std::string mPluginId;
    const PluginManifest *mManifest = nullptr;
};

/// HostServices handed to the plugin; values arrive with plugin.load. The
/// log sink forwards to the launcher as fire-and-forget events (thread-safe:
/// the channel serializes frame writes).
class WorkerHostServices : public HostServicesV1
{
public:
    WorkerHostServices( std::string tempDirectory, std::string workspaceRoot,
                        std::string dataDirectory, std::string pluginDirectory )
        : mTemp( std::move( tempDirectory ) )
        , mWorkspace( std::move( workspaceRoot ) )
        , mData( std::move( dataDirectory ) )
        , mPlugin( std::move( pluginDirectory ) )
    {
    }

    std::string tempDirectory() const override { return mTemp; }
    std::string workspaceRoot() const override { return mWorkspace; }
    std::string dataDirectory() const override { return mData; }
    std::string pluginDirectory() const override { return mPlugin; }
    void log( const char *level, const std::string &message ) override
    {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( !mChannel )
            return;
        Json::Value params( Json::objectValue );
        params["level"] = level;
        params["message"] = message;
        mChannel->sendEvent( "plugin.log", params );
    }

    void attachChannel( IpcChannel *channel )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mChannel = channel;
    }

private:
    std::string mTemp;
    std::string mWorkspace;
    std::string mData;
    std::string mPlugin;
    std::mutex mMutex;
    IpcChannel *mChannel = nullptr;
};

/// Runs one operator request on the dispatch thread. Progress flows back as
/// progress frames; cancellation is cooperative through @p cancelled.
void executeOperator( IpcChannel &channel, long long requestId, WorkerSink &sink,
                      const Json::Value &params, const std::atomic<bool> &cancelled,
                      const std::string &pluginId )
{
    const std::string operatorId = params.get( "operatorId", "" ).asString();
    auto factoryIterator = sink.mOperatorFactories.find( operatorId );
    if ( factoryIterator == sink.mOperatorFactories.end() )
    {
        channel.sendError( requestId,
                           { "E6008", "operator not registered by this plugin: " + operatorId } );
        return;
    }

    std::unique_ptr<sicnu::operators::RSOperator> instance;
    try
    {
        instance = factoryIterator->second();
    }
    catch ( const std::exception &exception )
    {
        channel.sendError( requestId,
                           { "E4004",
                             std::string( "operator factory threw: " ) + exception.what() } );
        return;
    }
    if ( !instance )
    {
        channel.sendError( requestId, { "E4004", "operator factory returned nullptr" } );
        return;
    }

    const std::string workDir = params.get( "workDir", "" ).asString();
    if ( !workDir.empty() )
    {
        std::error_code ec;
        std::filesystem::create_directories( workDir, ec );
    }
    sicnu::operators::RSOperatorContext context( workDir );
    context.setProgressCallback(
        [&]( double progress, const std::string &message ) {
            channel.sendProgress( requestId, progress, message );
        } );
    context.setCancelCallback( [&cancelled]() -> bool { return cancelled.load(); } );
    context.setLogCallback( [&]( const std::string &message, const std::string &level ) {
        channel.sendEvent( "operator.log",
                           [] ( std::string id, std::string lvl, std::string msg ) {
                               Json::Value params( Json::objectValue );
                               params["operatorId"] = std::move( id );
                               params["level"] = std::move( lvl );
                               params["message"] = std::move( msg );
                               return params;
                           }( operatorId, level, message ) );
    } );

    try
    {
        const Json::Value runResult =
            instance->run( params.get( "params", Json::Value( Json::objectValue ) ), context );
        Json::Value envelope( Json::objectValue );
        envelope["success"] = true;
        envelope["result"] = runResult;
        envelope["plugin"] = pluginId;
        std::string error;
        channel.sendResponse( requestId, envelope, error );
    }
    catch ( const sicnu::operators::RSOperatorError &error )
    {
        IpcError ipcError;
        ipcError.code = error.code() == sicnu::operators::ErrorCode::Cancelled
                            ? std::string( "E6009" )
                            : std::to_string( static_cast<int>( error.code() ) );
        ipcError.message = error.message();
        ipcError.data = error.details();
        ipcError.retryable = error.code() == sicnu::operators::ErrorCode::Cancelled;
        channel.sendError( requestId, ipcError );
    }
    catch ( const std::exception &exception )
    {
        channel.sendError( requestId,
                           { "9999", std::string( "operator run() threw: " ) + exception.what() } );
    }
}

} // namespace

int main( int argc, char **argv )
{
    std::string readHandle;
    std::string writeHandle;
    for ( int index = 1; index < argc; ++index )
    {
        const std::string argument = argv[ index ];
        if ( argument.rfind( kIpcReadSwitch, 0 ) == 0 )
            readHandle = argument.substr( std::strlen( kIpcReadSwitch ) );
        else if ( argument.rfind( kIpcWriteSwitch, 0 ) == 0 )
            writeHandle = argument.substr( std::strlen( kIpcWriteSwitch ) );
    }
    if ( readHandle.empty() || writeHandle.empty() )
        return kExitUsage;

#ifdef _WIN32
    HANDLE readH = nullptr;
    HANDLE writeH = nullptr;
    {
        std::istringstream stream( readHandle );
        long long value = 0;
        stream >> std::hex >> value;
        readH = reinterpret_cast<HANDLE>( static_cast<intptr_t>( value ) );
        std::istringstream stream2( writeHandle );
        stream2 >> std::hex >> value;
        writeH = reinterpret_cast<HANDLE>( static_cast<intptr_t>( value ) );
    }
    void *readRaw = readH;
    void *writeRaw = writeH;
#else
    void *readRaw =
        reinterpret_cast<void *>( static_cast<intptr_t>( std::atoi( readHandle.c_str() ) ) );
    void *writeRaw =
        reinterpret_cast<void *>( static_cast<intptr_t>( std::atoi( writeHandle.c_str() ) ) );
#endif
    if ( !ipcHandleStreamHandlesValid( readRaw, writeRaw ) )
        return kExitUsage;

#ifdef _WIN32
    // A crashing plugin must DIE, not open a WER report dialog: the debug
    // CRT and default error mode would otherwise hold this process open
    // until the launcher's kill ladder fires (observed with the crash
    // fixture). Suppress UI and let the fault terminate the process.
    ::SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                    SEM_NOOPENFILEERRORBOX );
#endif

    // Protocol channel on the dedicated handles — plugin stdout noise is
    // structurally irrelevant to framing.
    IpcChannel channel( makeIpcHandleStream( readRaw, writeRaw ) );

    // Handshake BEFORE any plugin code runs.
    {
        Json::Value hello( Json::objectValue );
        hello["protocolMajor"] = hostProtocolVersionMajor();
        hello["protocolMinor"] = hostProtocolVersionMinor();
        hello["apiVersion"] = EXP_RS_PLUGIN_API_VERSION;
        hello["abiVersion"] = pluginAbiVersion();
        hello["manifestVersion"] = supportedManifestVersion();
#ifdef _WIN32
        hello["platform"] = "windows";
#elif defined( __APPLE__ )
        hello["platform"] = "macos";
#else
        hello["platform"] = "linux";
#endif
        channel.sendEvent( kWorkerHello, hello );
    }

    PluginManifest manifest;
    bool manifestLoaded = false;
    WorkerSink sink;
    std::unique_ptr<WorkerHostServices> services;
    LoadedPlugin loadedInstance;
    bool instanceValid = false;

    // Cooperative cancellation: reset per request (serial dispatch in v1).
    std::atomic<bool> cancelled{ false };
    long long executingRequestId = 0;
    channel.setCancelSink( [&]( long long id ) {
        if ( id == -1 || id == executingRequestId )
            cancelled = true;
    } );

    Ipc::Envelope request;
    for ( ;; )
    {
        // nextRequest returns false BOTH on close and on the idle
        // timeout: an idle worker must keep serving (a GUI host may not
        // call the plugin for minutes), so only a genuinely closed
        // channel ends the serve loop.
        if ( !channel.nextRequest( request, 60000 ) )
        {
            if ( channel.isOpen() )
                continue;
            break;
        }
        if ( request.method == kLoadPlugin )
        {
            if ( instanceValid )
            {
                channel.sendError( request.id,
                                   { "E6002", "plugin already loaded in this worker" } );
                continue;
            }
            const Json::Value &params = request.params;
            PluginDiagnostic parseError;
            PluginManifest parsed;
            if ( !PluginManifest::fromJson( params["manifest"], parsed, parseError ) )
            {
                channel.sendError( request.id,
                                   { "E1002", "worker could not parse the manifest" } );
                continue;
            }
            manifest = parsed;
            manifestLoaded = true;
            sink.setManifest( &manifest );

            const Json::Value &serviceValues = params["services"];
            services = std::make_unique<WorkerHostServices>(
                serviceValues.get( "tempDirectory", "" ).asString(),
                serviceValues.get( "workspaceRoot", "" ).asString(),
                serviceValues.get( "dataDirectory", "" ).asString(),
                serviceValues.get( "pluginDirectory", "" ).asString() );
            services->attachChannel( &channel );

            PluginRecord record;
            record.manifest = manifest;
            record.directory = params.get( "pluginDirectory", "" ).asString();

            PluginLoader loader;
            PluginDiagnosticLog loadLog;
            if ( !loader.load( record, *services, sink, loadLog ) )
            {
                Json::Value logJson = loadLog.toJson();
                IpcError error;
                error.code = "E4002";
                error.message = "worker failed to load the plugin binary";
                error.data = logJson;
                channel.sendError( request.id, error );
                // Reset state: the worker stays usable for nothing else —
                // a one-plugin-per-process isolation guarantee means the
                // launcher will kill this worker and start fresh.
                manifest = PluginManifest();
                manifestLoaded = false;
                sink.setManifest( nullptr );
                services.reset();
                continue;
            }
            loadedInstance = loader.take();
            instanceValid = true;

            Json::Value registered( Json::objectValue );
            Json::Value operators( Json::arrayValue );
            for ( const auto &entry : sink.mOperatorFactories )
                operators.append( entry.first );
            Json::Value providers( Json::arrayValue );
            for ( const auto &entry : sink.mDataProviders )
                providers.append( entry.first );
            Json::Value runtimes( Json::arrayValue );
            for ( const auto &entry : sink.mModelRuntimeFactories )
                runtimes.append( entry.first );
            Json::Value tools( Json::arrayValue );
            for ( const auto &entry : sink.mAgentTools )
                tools.append( entry.first );
            registered["operators"] = operators;
            registered["dataProviders"] = providers;
            registered["modelRuntimes"] = runtimes;
            registered["agentTools"] = tools;
            Json::Value result( Json::objectValue );
            result["registered"] = registered;
            std::string sendError;
            channel.sendResponse( request.id, result, sendError );
        }
        else if ( request.method == kExecuteOperator )
        {
            if ( !instanceValid )
            {
                channel.sendError( request.id, { "E4003", "plugin is not loaded" } );
                continue;
            }
            cancelled = false;
            executingRequestId = request.id;
            executeOperator( channel, request.id, sink, request.params, cancelled,
                             manifest.id );
            executingRequestId = 0;
        }
        else if ( request.method == kExecuteAgentTool )
        {
            if ( !instanceValid )
            {
                channel.sendError( request.id, { "E4003", "plugin is not loaded" } );
                continue;
            }
            const std::string toolId = request.params.get( "toolId", "" ).asString();
            auto toolIterator = sink.mAgentTools.find( toolId );
            if ( toolIterator == sink.mAgentTools.end() )
            {
                channel.sendError( request.id,
                                   { "E6008", "agent tool not registered: " + toolId } );
                continue;
            }
            cancelled = false;
            executingRequestId = request.id;
            try
            {
                Json::Value envelope =
                    toolIterator->second->execute( request.params["params"] );
                std::string sendError;
                channel.sendResponse( request.id, envelope, sendError );
            }
            catch ( const std::exception &exception )
            {
                channel.sendError( request.id,
                                   { "E4003",
                                     std::string( "agent tool execute() threw: " )
                                         + exception.what() } );
            }
            executingRequestId = 0;
        }
        else if ( request.method == kShutdownPlugin )
        {
            if ( instanceValid )
            {
                PluginLoader loader;
                PluginDiagnosticLog unloadLog;
                loader.unload( loadedInstance, unloadLog );
                instanceValid = false;
            }
            Json::Value result( Json::objectValue );
            result["shutdown"] = true;
            std::string sendError;
            channel.sendResponse( request.id, result, sendError );
            channel.close();
            return kExitOk;
        }
        else
        {
            channel.sendError( request.id,
                               { "E6008", "worker does not implement method: " + request.method } );
        }
    }

    // Channel closed (launcher died or crashed): still honour the plugin's
    // shutdown contract inside the worker, then let the process exit.
    if ( instanceValid )
    {
        PluginLoader loader;
        PluginDiagnosticLog unloadLog;
        loader.unload( loadedInstance, unloadLog );
    }
    return kExitOk;
}
