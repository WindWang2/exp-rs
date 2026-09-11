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
#include "exprs/plugin_capabilities.h"
#include "exprs/ipc_envelope.h"
#include "exprs/ipc_frame.h"
#include "exprs/ipc_stream.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_manifest.h"
#include "exprs/plugin_ui_schema.h"
#include "exprs/version.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "plugins/host/plugin_host_protocol.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
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
#else
#include <unistd.h>   // _exit
#endif

using namespace exprs;
using namespace sicnu::plugins::hostprotocol;

extern "C" {
/// Resolved with dlsym/GetProcAddress from the plugin binary when present
/// (declared here so the worker can resolve it without SDK-loader changes).
typedef exprs::UiSchemaProviderV1 *( *CreateUiSchemaProviderV1Fn )();
}

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
    const PluginManifest *manifest() const { return mManifest; }

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
            try
            {
                task();
            }
            catch ( const std::exception &exception )
            {
                // A pool task must never kill the worker (the host would
                // see a crash and burn a restart): answers were the task's
                // responsibility; a throw here leaves the request to time
                // out typed on the host instead.
                std::fprintf( stderr, "exprs_plugin_host_worker: task threw: %s\n",
                              exception.what() );
            }
            catch ( ... )
            {
                std::fprintf( stderr, "exprs_plugin_host_worker: task threw (unknown)\n" );
            }
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

/// Capability policy materialized at plugin.load (worker-side gate for the
/// host-provided operator workDir seam). Deny-by-default: with no declared
/// write roots, only the plugin-scoped temp directory is writable.
struct WorkerPolicy
{
    std::vector<std::string> writeRoots;
    std::string tempDirectory;
    bool active = false;

    bool allowsWorkDir( const std::string &workDir, std::string &resolved ) const
    {
        if ( workDir.empty() )
            return true;
        if ( !tempDirectory.empty() && pathIsWithinRoot( workDir, tempDirectory, resolved ) )
            return true;
        for ( const std::string &root : writeRoots )
            if ( pathIsWithinRoot( workDir, root, resolved ) )
                return true;
        return false;
    }
};

/// Runs one operator request on a pool thread. Progress flows back as
/// coalesced progress frames; cancellation is cooperative through
/// @p cancelled.
void executeOperator( IpcChannel &channel, long long requestId, WorkerSink &sink,
                      const Json::Value &params, const std::shared_ptr<std::atomic<bool>> &cancelled,
                      const std::string &pluginId, ProgressThrottle &throttle,
                      const WorkerPolicy &policy )
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
    std::string resolvedWorkDir;
    if ( !workDir.empty() && policy.active && !policy.allowsWorkDir( workDir, resolvedWorkDir ) )
    {
        // Typed policy refusal (fail closed): the host-provided workDir is
        // outside every declared write root and the plugin-scoped temp
        // directory. Honest boundary: this gates the workDir SEAM, not
        // arbitrary plugin I/O (see docs/plugins/capabilities.md).
        channel.sendError( requestId,
                           { "E5005", "operator workDir is outside the declared write roots "
                                      "and the plugin temp directory: " + workDir } );
        return;
    }
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
    // Capability gate (protocol 1.2): open/inspect URIs must carry a scheme
    // the manifest DECLARED for this provider. Enumeration (discover) stays
    // unfiltered — it is the listing surface. A provider that declares no
    // schemes keeps the pre-9.0 unrestricted behavior; scheme'd URIs outside
    // the declaration are a typed policy refusal. Honest boundary: plain
    // paths (no "://") are not scheme'd URIs and remain governed by the
    // filesystem seams, not by this gate.
    if ( request.method == kInspectData || request.method == kOpenData )
    {
        const std::string uri = params.get( "uri", "" ).asString();
        const size_t schemeEnd = uri.find( "://" );
        if ( schemeEnd != std::string::npos )
        {
            const std::string scheme = uri.substr( 0, schemeEnd );
            const ManifestDataProvider *declaration = nullptr;
            const PluginManifest *workerManifest = sink.manifest();
            if ( workerManifest )
            {
                for ( const ManifestDataProvider &candidate : workerManifest->dataProviders )
                {
                    if ( candidate.id == providerId )
                    {
                        declaration = &candidate;
                        break;
                    }
                }
            }
            if ( declaration && !declaration->schemes.empty() )
            {
                bool declared = false;
                for ( const std::string &candidate : declaration->schemes )
                {
                    // Declarations may carry a trailing "://"; normalize.
                    std::string declaredScheme = candidate;
                    const size_t tail = declaredScheme.find( "://" );
                    if ( tail != std::string::npos )
                        declaredScheme.resize( tail );
                    if ( declaredScheme == scheme )
                    {
                        declared = true;
                        break;
                    }
                }
                if ( !declared )
                {
                    channel.sendError(
                        requestId,
                        { "E5005", "uri scheme '" + scheme
                                       + "' is not declared by this data provider" } );
                    return;
                }
            }
        }
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
    // kInferModel — malformed tensor JSON must answer typed, never throw
    // into the pool thread (P2 review remediation).
    PluginInferenceResultV1 result;
    try
    {
        PluginTensorV1 input = parseTensor( params["input"] );
        const std::string outputTensorName = params.get( "outputTensorName", "" ).asString();
        result = ModelRuntimeCache::instance().infer( framework, input, outputTensorName );
    }
    catch ( const std::exception &exception )
    {
        result.success = false;
        result.error = std::string( "malformed inference request: " ) + exception.what();
    }
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
    // "features" (protocol 1.2) advertises optional protocol capabilities;
    // a 1.1 host ignores the field, a 1.1 worker omits it.
    {
        Json::Value hello( Json::objectValue );
        hello["protocolMajor"] = hostProtocolVersionMajor();
        hello["protocolMinor"] = hostProtocolVersionMinor();
        hello["apiVersion"] = EXP_RS_PLUGIN_API_VERSION;
        hello["abiVersion"] = pluginAbiVersion();
        hello["manifestVersion"] = supportedManifestVersion();
        hello["maxConcurrentRequests"] = kWorkerMaxSlots;
        Json::Value features( Json::arrayValue );
        features.append( "directionalFrameCaps" );
        hello["features"] = features;
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
    WorkerPolicy policy;
    exprs::UiSchemaProviderV1 *uiProvider = nullptr;   // plugin-owned; delete before unload

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

            // Protocol 1.1/1.2 downward frame-cap negotiation: never raise a
            // cap, only lower it (write side clamps; a violating peer sees
            // E6003 and the channel dies). maxFrameBytes is the shared 1.1
            // fallback; maxRequestBytes/maxResponseBytes are the 1.2
            // per-direction bounds — on THIS side of the channel requests
            // are RECEIVED and responses/progress are SENT, so the split
            // fixes the 1.1 defect where a small maxResponseBytes also
            // capped host->worker request frames.
            const Json::Value &limits = params["limits"];
            if ( limits.isObject() )
            {
                if ( limits["maxFrameBytes"].isNumeric() )
                {
                    const Json::LargestInt hostCap = limits["maxFrameBytes"].asLargestInt();
                    if ( hostCap > 0 && hostCap <= 0xFFFFFFFFll )
                        channel.lowerFrameCap( static_cast<uint32_t>( hostCap ) );
                }
                if ( limits["maxRequestBytes"].isNumeric() )
                {
                    const Json::LargestInt cap = limits["maxRequestBytes"].asLargestInt();
                    if ( cap > 0 && cap <= 0xFFFFFFFFll )
                        channel.setDirectionalFrameCaps( channel.sendFrameCap(),
                                                         static_cast<uint32_t>( cap ) );
                }
                if ( limits["maxResponseBytes"].isNumeric() )
                {
                    const Json::LargestInt cap = limits["maxResponseBytes"].asLargestInt();
                    if ( cap > 0 && cap <= 0xFFFFFFFFll )
                        channel.setDirectionalFrameCaps( static_cast<uint32_t>( cap ),
                                                         channel.recvFrameCap() );
                }
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

            // Capability policy for the worker-side workDir gate: re-derived
            // HERE from the manifest the worker parsed itself plus its own
            // materialized services (no extra trust in launcher params).
            {
                PluginCapabilityParseResult access = parsePluginAccess(
                    manifest.access, record.directory,
                    serviceValues.get( "workspaceRoot", "" ).asString(),
                    serviceValues.get( "tempDirectory", "" ).asString() );
                policy.writeRoots = access.capabilities.fsWriteRoots;
                policy.tempDirectory = serviceValues.get( "tempDirectory", "" ).asString();
                // Opt-in containment: only plugins that DECLARE write roots
                // get the workDir gate. Without declarations the
                // host-provided default workDir (the executor's run
                // directory) is legitimate and is not gated — identical to
                // v1 behavior for manifests that make no capability claims.
                policy.active = access.ok() && !policy.writeRoots.empty();
            }

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

            // Optional declarative-UI provider (protocol 1.1): probe the
            // optional entry point. Absence is the normal "no UI" answer.
            if ( void *symbol = PluginLoader::resolveLibrarySymbol(
                     loadedInstance.libraryHandle, "EXPRS_createUiSchemaProviderV1" ) )
            {
                CreateUiSchemaProviderV1Fn create =
                    reinterpret_cast<CreateUiSchemaProviderV1Fn>( symbol );
                if ( exprs::UiSchemaProviderV1 *provider = create() )
                    uiProvider = provider;
            }

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
            if ( uiProvider )
            {
                delete uiProvider;   // plugin code is still mapped here
                uiProvider = nullptr;
            }
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
        else if ( request.method == kDescribeUi || request.method == kInvokeUi )
        {
            // Declarative UI (protocol 1.1). ui.describe is answered on the
            // main thread (one-shot, validated); ui.invoke may interleave
            // with executions and runs on the pool.
            if ( !instanceValid || !uiProvider )
            {
                // E6008 is the protocol's "no such surface" answer: for
                // ui.describe it means "the plugin offers no UI".
                channel.sendError( request.id,
                                   { "E6008", "plugin provides no declarative UI" } );
                continue;
            }
            if ( request.method == kDescribeUi )
            {
                Json::Value schema;
                try
                {
                    schema = uiProvider->describeUi();
                }
                catch ( const std::exception &exception )
                {
                    channel.sendError( request.id,
                                       { "E4003",
                                         std::string( "describeUi() threw: " )
                                             + exception.what() } );
                    continue;
                }
                exprs::PluginUiSchemaParseResult validated;
                try
                {
                    validated = exprs::validatePluginUiSchema( schema );
                }
                catch ( const std::exception &exception )
                {
                    // A hostile schema must fail typed, never kill the
                    // worker (validation digs into plugin-controlled JSON).
                    channel.sendError( request.id,
                                       { "E5005",
                                         std::string( "plugin UI schema validation threw: " )
                                             + exception.what() } );
                    continue;
                }
                if ( !validated.ok() )
                {
                    Json::Value details( Json::arrayValue );
                    for ( const std::string &error : validated.errors )
                        details.append( error );
                    IpcError refusal;
                    refusal.code = "E5005";
                    refusal.message = "plugin UI schema failed validation";
                    refusal.data = details;
                    channel.sendError( request.id, refusal );
                    continue;
                }
                Json::Value result( Json::objectValue );
                result["schema"] = validated.normalized;
                std::string sendError;
                channel.sendResponse( request.id, result, sendError );
                continue;
            }
            // ui.invoke -> pool (bounded, may wait on plugin state).
            const Ipc::Envelope uiRequest = std::move( request );
            auto uiCancelled = cancels.registerRequest( uiRequest.id );
            const bool posted = pool.post( [this_ = &channel, &cancels, uiProvider, uiCancelled,
                                            uiRequest] {
                (void)uiCancelled; // events are cooperative; the plugin answers
                struct Unregister
                {
                    CancelRegistry *registry;
                    long long id;
                    ~Unregister() { registry->unregisterRequest( id ); }
                } unregister{ &cancels, uiRequest.id };
                Json::Value response;
                try
                {
                    response = uiProvider->handleUiEvent( uiRequest.params["event"] );
                }
                catch ( const std::exception &exception )
                {
                    this_->sendError( uiRequest.id,
                                      { "E4003",
                                        std::string( "handleUiEvent() threw: " )
                                            + exception.what() } );
                    return;
                }
                Json::Value result( Json::objectValue );
                result["response"] = response;
                std::string sendError;
                this_->sendResponse( uiRequest.id, result, sendError );
            } );
            if ( !posted )
                cancels.unregisterRequest( uiRequest.id );
            if ( !posted )
                channel.sendError( uiRequest.id,
                                   { "E6002", "worker is shutting down; request refused" } );
        }
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
            // Registered BEFORE queueing: a cancel frame arriving while the
            // request waits for a pool slot must not be lost.
            auto cancelled = cancels.registerRequest( executionRequest.id );
            const bool posted = pool.post( [this_ = &channel, &cancels, &throttle, &sink,
                                            &manifest, &policy, cancelled, instanceValid,
                                            executionRequest] {
                const Ipc::Envelope &request = executionRequest;
                if ( request.method == kExecuteOperator )
                    executeOperator( *this_, request.id, sink, request.params, cancelled,
                                     manifest.id, throttle, policy );
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
            {
                cancels.unregisterRequest( executionRequest.id );
                channel.sendError( executionRequest.id,
                                   { "E6002", "worker is shutting down; request refused" } );
            }
        }
        else
        {
            channel.sendError( request.id,
                               { "E6008", "worker does not implement method: " + request.method } );
        }
    }

    // Channel closed (launcher died or crashed): still honour the plugin's
    // shutdown contract inside the worker, then let the process exit.
    const bool drained = poolStarted ? pool.drain( kShutdownDrainTimeoutMs ) : true;
    if ( uiProvider )
    {
        delete uiProvider;
        uiProvider = nullptr;
    }
    if ( !drained )
    {
        // A wedged operator thread was DETACHED (cannot be joined): its
        // stack may reference main()'s locals. Never return through them —
        // exit the process right here (the launcher already saw EOF).
        ::_exit( kExitOk );
    }
    if ( instanceValid )
    {
        PluginLoader loader;
        PluginDiagnosticLog unloadLog;
        loader.unload( loadedInstance, unloadLog );
    }
    return kExitOk;
}
