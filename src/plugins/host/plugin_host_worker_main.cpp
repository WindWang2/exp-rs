/***************************************************************************
 * src/plugins/host/plugin_host_worker_main.cpp — exprs_plugin_host_worker
 *
 * The out-of-process host for ONE native plugin (isolation runtime 5.0;
 * protocol 1.1 since plugin-platform 8.0). The worker maps the plugin
 * binary in ITS OWN process through the standard exprs::PluginLoader (V1
 * entry point — no new ABI), drives the lifecycle and answers requests
 * over the versioned IPC contract. A plugin crash kills the worker, never
 * the ExpRS host process; the launcher applies its restart policy.
 *
 * Protocol handles arrive as inherited OS handles in argv (never stdio —
 * plugin printf noise lands on the worker's real stdout, a sink, and
 * cannot corrupt framing). Startup sequence:
 *   1. parse --exprs-ipc-read/--exprs-ipc-write
 *   2. send worker.hello (protocol/API/ABI axes + dispatch capability)
 *      BEFORE any plugin code
 *   3. serve: plugin.load → execution pool → contribution registration →
 *      concurrent execution requests (width negotiated from the host's
 *      quota through plugin.load "limits")
 *   4. plugin.shutdown → drain in-flight (bounded) → PluginV1::shutdown +
 *      dlclose → exit 0
 *
 * Dispatch model (protocol 1.1): lifecycle requests (plugin.load /
 * plugin.shutdown) run on the main thread; execution requests
 * (operator.execute, agentTool.execute, dataProvider.*, modelRuntime.*)
 * run on a bounded pool. Per-request cancel frames route through a map of
 * cooperative-cancel flags; the broadcast id (-1) cancels everything.
 * Progress frames are coalesced per request (bounded frame rate).
 *
 * NOTE on v1 → v1.1: the data-provider and model-runtime proxy methods
 * were DECLARED in v1 but the v1 worker never answered them (E6008); v1.1
 * implements them. Contributions still register in v1, so manifests are
 * unchanged.
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
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace exprs;
using namespace sicnu::plugins::hostprotocol;

namespace {

/// Hard ceiling of the worker's own dispatch width (the host clamps further
/// through its quota gate; this bounds worker threads regardless of what a
/// host asks for). Reported in worker.hello as maxConcurrentRequests.
constexpr int kWorkerMaxSlots = 8;

/// Minimum interval between two progress frames for the same request (a
/// chatty operator cannot flood the channel; the latest value still rides
/// the final response or a later throttled frame).
constexpr int kProgressThrottleMs = 20;

/// Bounded drain window for plugin.shutdown before the unload proceeds
/// (a stuck operator delays only its own process exit; the launcher's
/// kill ladder is the backstop).
constexpr int kShutdownDrainTimeoutMs = 10000;

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

/// Bounded task pool for execution requests (protocol 1.1). Width is fixed
/// at plugin.load from the host-negotiated limit; tasks run on pool threads
/// while the main thread keeps pulling frames.
class ExecutionPool
{
public:
    using Task = std::function<void()>;

    /// Starts the pool exactly once (a later plugin.load in the same worker
    /// is refused anyway — one plugin per process).
    void start( int width )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( mStarted )
            return;
        mWidth = width < 1 ? 1 : ( width > kWorkerMaxSlots ? kWorkerMaxSlots : width );
        for ( int i = 0; i < mWidth; ++i )
            mThreads.emplace_back( [this] { workerLoop(); } );
        mStarted = true;
    }

    /// Queues one task. False when the pool is not accepting (drained).
    bool post( Task task )
    {
        {
            std::lock_guard<std::mutex> lock( mMutex );
            if ( !mStarted || mDraining )
                return false;
            mTasks.push_back( std::move( task ) );
            ++mOutstanding;
        }
        mCv.notify_one();
        return true;
    }

    int width() const
    {
        std::lock_guard<std::mutex> lock( mMutex );
        return mWidth;
    }

    /// Stops accepting new tasks and waits (bounded) until everything
    /// queued has run. Returns false when tasks were still running at the
    /// deadline (the worker exits anyway; the launcher ladder backstops).
    bool drain( int timeoutMs )
    {
        {
            std::lock_guard<std::mutex> lock( mMutex );
            if ( !mStarted )
                return true;
            mDraining = true;
        }
        mCv.notify_all();
        const auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::milliseconds( timeoutMs > 0 ? timeoutMs : 0 );
        for ( ;; )
        {
            bool idle;
            {
                std::lock_guard<std::mutex> lock( mMutex );
                idle = mOutstanding == 0;
            }
            if ( idle || std::chrono::steady_clock::now() >= deadline )
                break;
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        bool idle;
        {
            std::lock_guard<std::mutex> lock( mMutex );
            idle = mOutstanding == 0;
        }
        if ( idle )
        {
            // Clean drain: every thread sees draining+empty and exits.
            mCv.notify_all();
            for ( std::thread &thread : mThreads )
            {
                if ( thread.joinable() )
                    thread.join();
            }
        }
        else
        {
            // Wedged operator: cannot join a running thread. The worker is
            // about to exit(0)/be killed by the launcher's ladder — the
            // wedged thread dies with the process. Detach so main() never
            // blocks on it.
            for ( std::thread &thread : mThreads )
            {
                if ( thread.joinable() )
                    thread.detach();
            }
        }
        {
            std::lock_guard<std::mutex> lock( mMutex );
            mThreads.clear();
        }
        return idle;
    }

    ~ExecutionPool()
    {
        drain( 1000 );
    }

private:
    void workerLoop()
    {
        for ( ;; )
        {
            Task task;
            {
                std::unique_lock<std::mutex> lock( mMutex );
                mCv.wait( lock, [this] { return mDraining || !mTasks.empty(); } );
                if ( mTasks.empty() )
                    return;   // draining and nothing left
                task = std::move( mTasks.front() );
                mTasks.pop_front();
            }
            task();
            {
                std::lock_guard<std::mutex> lock( mMutex );
                --mOutstanding;
            }
            mCv.notify_all();
        }
    }

    mutable std::mutex mMutex;
    std::condition_variable mCv;
    std::deque<Task> mTasks;
    std::vector<std::thread> mThreads;
    int mWidth = 1;
    int mOutstanding = 0;
    bool mDraining = false;
    bool mStarted = false;
};

/// Per-request cooperative-cancel registry. The reader thread delivers
/// cancel frames here; execution threads look their flag up by request id.
class CancelRegistry
{
public:
    std::shared_ptr<std::atomic<bool>> registerRequest( long long id )
    {
        auto flag = std::make_shared<std::atomic<bool>>( false );
        std::lock_guard<std::mutex> lock( mMutex );
        mFlags[ id ] = flag;
        return flag;
    }

    void unregisterRequest( long long id )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mFlags.erase( id );
    }

    /// id -1 cancels EVERYTHING (broadcast).
    void cancel( long long id )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( id == -1 )
        {
            for ( auto &[ key, flag ] : mFlags )
            {
                (void)key;
                flag->store( true );
            }
            return;
        }
        auto it = mFlags.find( id );
        if ( it != mFlags.end() )
            it->second->store( true );
    }

private:
    std::mutex mMutex;
    std::map<long long, std::shared_ptr<std::atomic<bool>>> mFlags;
};

/// Progress frames are coalesced per request (protocol 1.1): at most one
/// frame per throttle window per request id.
class ProgressThrottle
{
public:
    bool shouldSend( long long id )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        const auto now = std::chrono::steady_clock::now();
        State &state = mStates[ id ];
        if ( state.lastSend + std::chrono::milliseconds( kProgressThrottleMs ) <= now )
        {
            state.lastSend = now;
            return true;
        }
        return false;
    }

    void forget( long long id )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mStates.erase( id );
    }

private:
    struct State
    {
        std::chrono::steady_clock::time_point lastSend{};
    };
    std::mutex mMutex;
    std::map<long long, State> mStates;
};

/// Loaded model runtimes live in the worker for the process lifetime (one
/// plugin per process); the launcher only holds descriptors.
class ModelRuntimeCache
{
public:
    static ModelRuntimeCache &instance()
    {
        static ModelRuntimeCache cache;
        return cache;
    }

    bool load( const std::string &framework, PluginModelRuntimeFactoryV1 factory,
               const Json::Value &requestJson, Json::Value &result, IpcError &error )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( mRuntimes.count( framework ) )
        {
            result["backendName"] = mRuntimes[ framework ]->backendName();
            result["deviceName"] = mRuntimes[ framework ]->deviceName();
            return true; // idempotent re-load for the same framework
        }
        PluginModelRequestV1 modelRequest;
        modelRequest.modelName = requestJson.get( "modelName", "" ).asString();
        modelRequest.artifactPath = requestJson.get( "artifactPath", "" ).asString();
        modelRequest.manifest = requestJson["manifest"];
        modelRequest.gpuRequested = requestJson.get( "gpuRequested", false ).asBool();
        std::string loadError;
        auto runtime = factory( modelRequest, loadError );
        if ( !runtime )
        {
            error.code = "E4003";
            error.message = loadError.empty() ? "model runtime factory failed" : loadError;
            return false;
        }
        result["backendName"] = runtime->backendName();
        result["deviceName"] = runtime->deviceName();
        mRuntimes[ framework ] = std::move( runtime );
        return true;
    }

    PluginInferenceResultV1 infer( const std::string &framework, const PluginTensorV1 &input,
                                   const std::string &outputTensorName )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        auto it = mRuntimes.find( framework );
        if ( it == mRuntimes.end() )
        {
            PluginInferenceResultV1 failed;
            failed.error = "model runtime is not loaded (E4003): " + framework;
            return failed;
        }
        return it->second->infer( input, outputTensorName );
    }

private:
    ModelRuntimeCache() = default;
    std::mutex mMutex;
    std::map<std::string, PluginModelRuntimePtrV1> mRuntimes;
};

PluginTensorV1 parseTensor( const Json::Value &json )
{
    PluginTensorV1 tensor;
    tensor.batch = json.get( "batch", 1 ).asInt();
    tensor.channels = json.get( "channels", 1 ).asInt();
    tensor.rows = json.get( "rows", 0 ).asInt();
    tensor.cols = json.get( "cols", 0 ).asInt();
    if ( json["data"].isArray() )
    {
        tensor.data.reserve( json["data"].size() );
        for ( const Json::Value &value : json["data"] )
            tensor.data.push_back( value.asFloat() );
    }
    return tensor;
}

Json::Value tensorToJson( const PluginTensorV1 &tensor )
{
    Json::Value json( Json::objectValue );
    json["batch"] = tensor.batch;
    json["channels"] = tensor.channels;
    json["rows"] = tensor.rows;
    json["cols"] = tensor.cols;
    Json::Value data( Json::arrayValue );
    for ( const float value : tensor.data )
        data.append( value );
    json["data"] = data;
    return json;
}

/// Runs one operator request on a pool thread. Progress flows back as
/// coalesced progress frames; cancellation is cooperative through
/// @p cancelled.
void executeOperator( IpcChannel &channel, long long requestId, WorkerSink &sink,
                      const Json::Value &params, const std::shared_ptr<std::atomic<bool>> &cancelled,
                      const std::string &pluginId, ProgressThrottle &throttle )
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
        [&, requestId]( double progress, const std::string &message ) {
            if ( throttle.shouldSend( requestId ) )
                channel.sendProgress( requestId, progress, message );
        } );
    context.setCancelCallback( [&cancelled]() -> bool { return cancelled->load(); } );
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

/// Answers one data-provider request on a pool thread (v1.1: these methods
/// were declared but never implemented in the v1 worker — E6008 refusals).
void executeDataProvider( IpcChannel &channel, long long requestId, WorkerSink &sink,
                          const Ipc::Envelope &request )
{
    const Json::Value &params = request.params;
    const std::string providerId = params.get( "providerId", "" ).asString();
    auto providerIterator = sink.mDataProviders.find( providerId );
    if ( providerIterator == sink.mDataProviders.end() )
    {
        channel.sendError( requestId, { "E6008", "data provider not registered: " + providerId } );
        return;
    }
    try
    {
        Json::Value result;
        if ( request.method == kDiscoverData )
            result = providerIterator->second->discover( params["query"] );
        else if ( request.method == kInspectData )
            result = providerIterator->second->inspect( params.get( "uri", "" ).asString() );
        else
            result = providerIterator->second->open( params.get( "uri", "" ).asString() );
        Json::Value envelope( Json::objectValue );
        envelope["result"] = result;
        std::string sendError;
        channel.sendResponse( request.id, envelope, sendError );
    }
    catch ( const std::exception &exception )
    {
        channel.sendError( requestId,
                           { "E4003", std::string( "data provider threw: " ) + exception.what() } );
    }
}

/// Answers one model-runtime request on a pool thread (v1.1).
void executeModelRuntime( IpcChannel &channel, long long requestId, WorkerSink &sink,
                          const Ipc::Envelope &request )
{
    const Json::Value &params = request.params;
    const std::string framework = params.get( "framework", "" ).asString();
    auto factoryIterator = sink.mModelRuntimeFactories.find( framework );
    if ( factoryIterator == sink.mModelRuntimeFactories.end() )
    {
        channel.sendError( requestId, { "E6008", "model runtime not registered: " + framework } );
        return;
    }
    if ( request.method == kLoadModel )
    {
        Json::Value result;
        IpcError error;
        if ( !ModelRuntimeCache::instance().load( framework, factoryIterator->second,
                                                  params["request"], result, error ) )
        {
            channel.sendError( requestId, error );
            return;
        }
        std::string sendError;
        channel.sendResponse( requestId, result, sendError );
        return;
    }
    // kInferModel
    PluginTensorV1 input = parseTensor( params["input"] );
    const std::string outputTensorName = params.get( "outputTensorName", "" ).asString();
    const PluginInferenceResultV1 result =
        ModelRuntimeCache::instance().infer( framework, input, outputTensorName );
    Json::Value envelope( Json::objectValue );
    envelope["success"] = result.success;
    envelope["error"] = result.error;
    envelope["output"] = tensorToJson( result.output );
    envelope["diagnostics"] = result.diagnostics;
    std::string sendError;
    channel.sendResponse( requestId, envelope, sendError );
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
    CancelRegistry cancels;
    ProgressThrottle throttle;

    // Handshake BEFORE any plugin code runs. maxConcurrentRequests is the
    // worker's dispatch CAPABILITY; the effective width is negotiated down
    // with plugin.load's "limits.maxConcurrentRequests" (host quota
    // authority) — the reported value is never the effective one.
    {
        Json::Value hello( Json::objectValue );
        hello["protocolMajor"] = hostProtocolVersionMajor();
        hello["protocolMinor"] = hostProtocolVersionMinor();
        hello["apiVersion"] = EXP_RS_PLUGIN_API_VERSION;
        hello["abiVersion"] = pluginAbiVersion();
        hello["manifestVersion"] = supportedManifestVersion();
        hello["maxConcurrentRequests"] = kWorkerMaxSlots;
#ifdef _WIN32
        hello["platform"] = "windows";
#elif defined( __APPLE__ )
        hello["platform"] = "macos";
#else
        hello["platform"] = "linux";
#endif
        channel.sendEvent( kWorkerHello, hello );
    }

    channel.setCancelSink( [&cancels]( long long id ) { cancels.cancel( id ); } );

    PluginManifest manifest;
    bool manifestLoaded = false;
    WorkerSink sink;
    std::unique_ptr<WorkerHostServices> services;
    LoadedPlugin loadedInstance;
    bool instanceValid = false;
    ExecutionPool pool;
    bool poolStarted = false;

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

        // ---- lifecycle requests: main thread only -------------------------
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

            // Protocol 1.1 downward frame-cap negotiation: never raise the
            // cap, only lower it to the host's bound (write side clamps; a
            // violating peer sees E6003 and the channel dies).
            const Json::Value &limits = params["limits"];
            if ( limits.isObject() && limits["maxFrameBytes"].isNumeric() )
            {
                const Json::LargestInt hostCap = limits["maxFrameBytes"].asLargestInt();
                if ( hostCap > 0 && hostCap <= 0xFFFFFFFFll )
                    channel.lowerFrameCap( static_cast<uint32_t>( hostCap ) );
            }

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

            // Dispatch width: min(host ask, worker capability), at least 1.
            int width = 1;
            if ( limits.isObject() && limits["maxConcurrentRequests"].isNumeric() )
                width = limits["maxConcurrentRequests"].asInt();
            pool.start( width );
            poolStarted = true;

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
            result["dispatchWidth"] = pool.width();
            std::string sendError;
            channel.sendResponse( request.id, result, sendError );
        }
        else if ( request.method == kShutdownPlugin )
        {
            // Drain: stop accepting pool tasks and wait (bounded) for
            // in-flight executions so plugin code is never unloaded under a
            // running thread. A wedged operator delays only its own worker
            // exit; the launcher's kill ladder is the backstop.
            if ( poolStarted )
                pool.drain( kShutdownDrainTimeoutMs );
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

        // ---- execution requests: bounded pool -----------------------------
        else if ( request.method == kExecuteOperator || request.method == kExecuteAgentTool
                  || request.method == kDiscoverData || request.method == kInspectData
                  || request.method == kOpenData || request.method == kLoadModel
                  || request.method == kInferModel )
        {
            if ( !instanceValid )
            {
                channel.sendError( request.id, { "E4003", "plugin is not loaded" } );
                continue;
            }
            const Ipc::Envelope executionRequest = std::move( request );
            const bool posted = pool.post( [this_ = &channel, &cancels, &throttle, &sink,
                                            &manifest, instanceValid, executionRequest] {
                const Ipc::Envelope &request = executionRequest;
                auto cancelled = cancels.registerRequest( request.id );
                if ( request.method == kExecuteOperator )
                    executeOperator( *this_, request.id, sink, request.params, cancelled,
                                     manifest.id, throttle );
                else if ( request.method == kExecuteAgentTool )
                {
                    const std::string toolId = request.params.get( "toolId", "" ).asString();
                    auto toolIterator = sink.mAgentTools.find( toolId );
                    if ( toolIterator == sink.mAgentTools.end() )
                    {
                        this_->sendError( request.id,
                                          { "E6008", "agent tool not registered: " + toolId } );
                    }
                    else
                    {
                        try
                        {
                            Json::Value envelope =
                                toolIterator->second->execute( request.params["params"] );
                            std::string sendError;
                            this_->sendResponse( request.id, envelope, sendError );
                        }
                        catch ( const std::exception &exception )
                        {
                            this_->sendError( request.id,
                                              { "E4003",
                                                std::string( "agent tool execute() threw: " )
                                                    + exception.what() } );
                        }
                    }
                }
                else if ( request.method == kDiscoverData || request.method == kInspectData
                          || request.method == kOpenData )
                {
                    executeDataProvider( *this_, request.id, sink, request );
                }
                else // kLoadModel / kInferModel
                {
                    executeModelRuntime( *this_, request.id, sink, request );
                }
                cancels.unregisterRequest( request.id );
                throttle.forget( request.id );
            } );
            if ( !posted )
                channel.sendError( executionRequest.id,
                                   { "E6002", "worker is shutting down; request refused" } );
        }
        else
        {
            channel.sendError( request.id,
                               { "E6008", "worker does not implement method: " + request.method } );
        }
    }

    // Channel closed (launcher died or crashed): still honour the plugin's
    // shutdown contract inside the worker, then let the process exit.
    if ( poolStarted )
        pool.drain( kShutdownDrainTimeoutMs );
    if ( instanceValid )
    {
        PluginLoader loader;
        PluginDiagnosticLog unloadLog;
        loader.unload( loadedInstance, unloadLog );
    }
    return kExitOk;
}
