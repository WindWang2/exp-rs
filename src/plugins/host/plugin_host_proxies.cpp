/***************************************************************************
 * src/plugins/host/plugin_host_proxies.cpp — launcher-side contribution
 * proxies. Each proxy marshals one contribution call over the IPC contract;
 * on worker death the operator proxy applies the bounded restart policy
 * (respawn + reload, armed atomically so a crash-looping plugin can never
 * stampede or spin the host); every other path fails typed.
 ***************************************************************************/
#include "plugin_host_proxies.h"

#include "plugin_host_protocol.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <atomic>
#include <cstdlib>
#include <mutex>

using namespace exprs;
using namespace sicnu::plugins;
using namespace sicnu::plugins::hostprotocol;

namespace {

int effectiveDeadline( const PluginQuota &quota, int requested )
{
    return requested > 0 ? std::min( requested, quota.requestDeadlineMs )
                         : quota.requestDeadlineMs;
}

/// One bounded recovery: armed atomically so concurrent failures do not
/// stampede; the restart policy itself lives in the runtime/session.
bool tryRecovery( PluginHostSessionEntry &entry )
{
    if ( !entry.runtime )
        return false;
    bool expected = false;
    if ( !entry.respawnArmed.compare_exchange_strong( expected, true ) )
        return false; // recovery already in flight; caller fails typed
    PluginDiagnosticLog recoveryLog;
    const bool recovered = entry.runtime->respawn( entry.pluginId, entry, recoveryLog );
    entry.respawnArmed = false;
    return recovered;
}

Json::Value typedFailure( const char *code, const std::string &message )
{
    Json::Value result( Json::objectValue );
    result["success"] = false;
    Json::Value error( Json::objectValue );
    error["code"] = code;
    error["message"] = message;
    result["error"] = error;
    return result;
}

} // namespace

namespace sicnu::plugins {

namespace {

/// Operator proxy: run() executes in the worker; progress streams back as
/// progress frames; cancellation is forwarded as a cancel frame.
class HostProcessOperatorProxy : public sicnu::operators::RSOperator
{
public:
    HostProcessOperatorProxy( PluginHostSessionEntryPtr entry, std::string pluginId,
                              std::string operatorId )
        : mEntry( std::move( entry ) )
        , mPluginId( std::move( pluginId ) )
        , mOperatorId( std::move( operatorId ) )
    {
    }

    std::string name() const override { return mOperatorId; }
    std::string displayName() const override { return mOperatorId; }
    std::string group() const override { return "plugin"; }
    std::string description() const override
    {
        return "host-process plugin operator (" + mPluginId + ")";
    }
    std::string determinismGrade() const override { return "tolerance"; }
    sicnu::operators::RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return sicnu::operators::RSOperatorMemoryPolicy::FullRaster;
    }
    sicnu::operators::RSOperatorDeterminism determinism() const override
    {
        return sicnu::operators::RSOperatorDeterminism::Tolerance;
    }
    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }
    // Defaulted virtuals below must be overridden here: the launcher links
    // sicnu_operators_core (contract symbols only), not the full operators
    // library that carries RSOperator's default implementations.
    Json::Value metadata() const override { return Json::Value( Json::objectValue ); }
    Json::Value executionEstimate() const override { return Json::Value( Json::objectValue ); }
    Json::Value estimateExecution( const Json::Value & ) const override
    {
        return Json::Value( Json::objectValue );
    }

    Json::Value run( const Json::Value &params,
                     sicnu::operators::RSOperatorContext &context ) override
    {
        // Two attempts: the first worker death applies the bounded restart
        // policy exactly once; everything else is a typed failure.
        // Locking note: entry.mutex protects session swaps; tryRecovery MUST
        // run WITHOUT it (respawn publishes under its own runtime lock, and
        // a non-recursive double-lock would throw EDEADLK).
        auto throwUnavailable = [&]( const std::string &why ) {
            throw sicnu::operators::RSOperatorError(
                sicnu::operators::ErrorCode::NotInitialized,
                "host-process worker for '" + mPluginId + "' " + why );
        };

        // Exactly ONE bounded recovery per call, whether the worker was
        // already dead on entry or died mid-execution (the restart
        // policy inside the runtime bounds total respawns per window).
        // Locking note: entry.mutex only guards the session SNAPSHOT (respawn
        // swaps it in place); the request itself runs UNLOCKED so protocol
        // 1.1 concurrency is real — the session is refcounted, so a respawn
        // can never invalidate the shared_ptr we are using (its channel
        // simply fails typed when the old worker dies).
        bool recovered = false;
        for ( int attempt = 0; attempt < 2; ++attempt )
        {
            std::shared_ptr<PluginHostProcessSession> session;
            {
                std::lock_guard<std::mutex> lock( mEntry->mutex );
                session = mEntry->session;
            }
            if ( !session || !session->isAlive() )
            {
                if ( recovered || !tryRecovery( *mEntry ) )
                    throwUnavailable( "crashed or exited (E6005); restart policy exhausted" );
                recovered = true;
                continue;
            }

            Json::Value requestParams( Json::objectValue );
            requestParams["operatorId"] = mOperatorId;
            requestParams["params"] = params;
            requestParams["workDir"] = context.workDir();

            // Host-side cooperative cancel (job engine / TaskCenter)
            // forwards as a per-id cancel frame (protocol 1.1): the worker
            // sets the request's flag and a well-behaved operator answers
            // E6009. Before 1.1 the host could only kill the whole worker.
            IpcChannel::Outcome outcome = session->request(
                kExecuteOperator, requestParams, effectiveDeadline( mEntry->quota, 0 ),
                [&context]() -> bool { return context.isCancelled(); },
                [&context]( double progress, const std::string &message ) {
                    context.reportProgress( progress, message );
                } );

            switch ( outcome.status )
            {
            case IpcChannel::Outcome::Status::Ok:
                return outcome.result["result"];
            case IpcChannel::Outcome::Status::Error:
            {
                if ( outcome.error.code == "E6009" )
                {
                    throw sicnu::operators::RSOperatorError(
                        sicnu::operators::ErrorCode::Cancelled, outcome.error.message,
                        outcome.error.data );
                }
                // Numeric codes are operator-taxonomy values straight from
                // the plugin. Structured E-codes (E5005 policy refusal,
                // E6008 unregistered id, ...) have no numeric counterpart:
                // they refuse as Unknown with the stable code preserved,
                // never coerced to ErrorCode(0) (Success).
                const int code = std::atoi( outcome.error.code.c_str() );
                throw sicnu::operators::RSOperatorError(
                    code != 0 ? static_cast<sicnu::operators::ErrorCode>( code )
                              : sicnu::operators::ErrorCode::Unknown,
                    outcome.error.message + " [" + outcome.error.code + "]",
                    outcome.error.data );
            }
            case IpcChannel::Outcome::Status::Timeout:
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::ExternalProcessTimeout,
                    "worker exceeded the request deadline (E6004); it was cancelled and killed" );
            case IpcChannel::Outcome::Status::Cancelled:
                throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled,
                                                         "request cancelled (E6009)" );
            case IpcChannel::Outcome::Status::ProtocolError:
                throw sicnu::operators::RSOperatorError(
                    sicnu::operators::ErrorCode::ComputationError,
                    "worker protocol violation (E6002/E6003): " + outcome.error.message );
            case IpcChannel::Outcome::Status::ChannelClosed:
            default:
                // Worker died mid-execution: the next loop iteration
                // applies ONE bounded recovery (or refuses typed if the
                // restart policy is exhausted).
                continue;
            }
        }
        throwUnavailable( "crashed (E6005); restart policy exhausted" );
    }

private:
    PluginHostSessionEntryPtr mEntry;
    std::string mPluginId;
    std::string mOperatorId;
};

/// Agent tool proxy: the SpatialTool envelope comes straight from the worker.
class HostProcessAgentToolProxy : public IPluginAgentToolV1
{
public:
    HostProcessAgentToolProxy( PluginHostSessionEntryPtr entry, std::string pluginId,
                               std::string toolId )
        : mEntry( std::move( entry ) )
        , mPluginId( std::move( pluginId ) )
        , mToolId( std::move( toolId ) )
    {
    }

    Json::Value execute( const Json::Value &params ) override
    {
        std::shared_ptr<PluginHostProcessSession> session;
        {
            std::lock_guard<std::mutex> lock( mEntry->mutex );
            session = mEntry->session;
        }
        if ( !session || !session->isAlive() )
            return typedFailure( "E6005", "host-process worker is not running" );
        Json::Value requestParams( Json::objectValue );
        requestParams["toolId"] = mToolId;
        requestParams["params"] = params;
        auto outcome = session->request( kExecuteAgentTool, requestParams,
                                         effectiveDeadline( mEntry->quota, 0 ) );
        if ( outcome.status == IpcChannel::Outcome::Status::Ok )
            return outcome.result;
        Json::Value envelope( Json::objectValue );
        envelope["success"] = false;
        Json::Value error( Json::objectValue );
        error["code"] = outcome.statusCode();
        error["message"] = outcome.error.message;
        envelope["error"] = error;
        return envelope;
    }

private:
    PluginHostSessionEntryPtr mEntry;
    std::string mPluginId;
    std::string mToolId;
};

/// Data provider proxy: discover/inspect/open marshal; results stay the
/// host-consumable references the in-process contract hands out (no raster
/// bytes ever traverse the channel).
class HostProcessDataProviderProxy : public IPluginDataProviderV1
{
public:
    HostProcessDataProviderProxy( PluginHostSessionEntryPtr entry, std::string pluginId,
                                  std::string providerId )
        : mEntry( std::move( entry ) )
        , mPluginId( std::move( pluginId ) )
        , mProviderId( std::move( providerId ) )
    {
    }

    Json::Value call( const char *method, const Json::Value &extra ) const
    {
        std::shared_ptr<PluginHostProcessSession> session;
        {
            std::lock_guard<std::mutex> lock( mEntry->mutex );
            session = mEntry->session;
        }
        if ( !session || !session->isAlive() )
            return typedFailure( "E6005", "host-process worker is not running" );
        Json::Value params( Json::objectValue );
        params["providerId"] = mProviderId;
        for ( const std::string &name : extra.getMemberNames() )
            params[name] = extra[name];
        auto outcome = session->request( method, params,
                                         effectiveDeadline( mEntry->quota, 0 ) );
        if ( outcome.status == IpcChannel::Outcome::Status::Ok )
            return outcome.result;
        return typedFailure( outcome.statusCode().c_str(), outcome.error.message );
    }

    Json::Value discover( const Json::Value &query ) override
    {
        Json::Value extra( Json::objectValue );
        extra["query"] = query;
        return call( kDiscoverData, extra );
    }
    Json::Value inspect( const std::string &uri ) override
    {
        Json::Value extra( Json::objectValue );
        extra["uri"] = uri;
        return call( kInspectData, extra );
    }
    Json::Value open( const std::string &uri ) override
    {
        Json::Value extra( Json::objectValue );
        extra["uri"] = uri;
        return call( kOpenData, extra );
    }
    Json::Value capabilities() const override
    {
        Json::Value extra( Json::objectValue );
        extra["capabilitiesOnly"] = true;
        return call( kDiscoverData, extra );
    }

private:
    PluginHostSessionEntryPtr mEntry;
    std::string mPluginId;
    std::string mProviderId;
};

/// Model runtime proxy: the model is loaded and inferenced INSIDE the
/// worker (GPU/memory stay in the crash domain); the proxy addresses it.
class HostProcessModelRuntimeProxy : public IPluginModelRuntimeV1
{
public:
    HostProcessModelRuntimeProxy( PluginHostSessionEntryPtr entry, std::string pluginId,
                                  std::string framework, const PluginModelRequestV1 &request,
                                  std::string &error )
        : mEntry( std::move( entry ) )
        , mPluginId( std::move( pluginId ) )
        , mFramework( std::move( framework ) )
    {
        Json::Value params( Json::objectValue );
        params["framework"] = mFramework;
        Json::Value requestJson( Json::objectValue );
        requestJson["modelName"] = request.modelName;
        requestJson["artifactPath"] = request.artifactPath;
        requestJson["manifest"] = request.manifest;
        requestJson["gpuRequested"] = request.gpuRequested;
        params["request"] = requestJson;
        // Consistent locking with every other proxy: snapshot the session
        // under the entry mutex, then request unlocked (baseline G11 fix).
        std::shared_ptr<PluginHostProcessSession> session;
        {
            std::lock_guard<std::mutex> lock( mEntry->mutex );
            session = mEntry->session;
        }
        auto outcome = session
                           ? session->request( kLoadModel, params,
                                               effectiveDeadline( mEntry->quota, 0 ) )
                           : IpcChannel::Outcome{};
        if ( outcome.status != IpcChannel::Outcome::Status::Ok )
        {
            error = outcome.error.message.empty() ? "worker model load failed"
                                                  : outcome.error.message;
            mLoaded = false;
            return;
        }
        mBackend = outcome.result.get( "backendName", "" ).asString();
        mDevice = outcome.result.get( "deviceName", "" ).asString();
        mLoaded = true;
    }

    std::string backendName() const override { return mBackend; }
    std::string deviceName() const override { return mDevice; }

    bool load( const PluginModelRequestV1 &, std::string &loadError ) override
    {
        loadError = mLoaded ? std::string() : "model runtime failed to load in the worker";
        return mLoaded;
    }

    PluginInferenceResultV1 infer( const PluginTensorV1 &input,
                                   const std::string &outputTensorName ) override
    {
        PluginInferenceResultV1 result;
        std::shared_ptr<PluginHostProcessSession> session;
        {
            std::lock_guard<std::mutex> lock( mEntry->mutex );
            session = mEntry->session;
        }
        if ( !mLoaded || !session || !session->isAlive() )
        {
            result.error = "model runtime is not loaded (E4003)";
            return result;
        }
        Json::Value params( Json::objectValue );
        params["framework"] = mFramework;
        Json::Value inputJson( Json::objectValue );
        Json::Value data( Json::arrayValue );
        for ( const float value : input.data )
            data.append( value );
        inputJson["data"] = data;
        inputJson["batch"] = input.batch;
        inputJson["channels"] = input.channels;
        inputJson["rows"] = input.rows;
        inputJson["cols"] = input.cols;
        params["input"] = inputJson;
        params["outputTensorName"] = outputTensorName;
        auto outcome = session->request( kInferModel, params,
                                         effectiveDeadline( mEntry->quota, 0 ) );
        if ( outcome.status != IpcChannel::Outcome::Status::Ok )
        {
            result.error = outcome.error.message;
            return result;
        }
        result.success = outcome.result.get( "success", false ).asBool();
        result.error = outcome.result.get( "error", "" ).asString();
        const Json::Value &output = outcome.result["output"];
        result.output.batch = output.get( "batch", 1 ).asInt();
        result.output.channels = output.get( "channels", 1 ).asInt();
        result.output.rows = output.get( "rows", 0 ).asInt();
        result.output.cols = output.get( "cols", 0 ).asInt();
        for ( const Json::Value &value : output["data"] )
            result.output.data.push_back( value.asFloat() );
        result.diagnostics = outcome.result["diagnostics"];
        return result;
    }

    std::vector<std::string> outputTensorNames() const override
    {
        return {};
    }

private:
    PluginHostSessionEntryPtr mEntry;
    std::string mPluginId;
    std::string mFramework;
    std::string mBackend;
    std::string mDevice;
    bool mLoaded = false;
};

} // namespace

std::unique_ptr<sicnu::operators::RSOperator> makeHostProcessOperatorProxy(
    PluginHostSessionEntryPtr entry, std::string pluginId, std::string operatorId )
{
    return std::make_unique<HostProcessOperatorProxy>( std::move( entry ), std::move( pluginId ),
                                                       std::move( operatorId ) );
}

std::shared_ptr<IPluginAgentToolV1> makeHostProcessAgentToolProxy(
    PluginHostSessionEntryPtr entry, std::string pluginId, std::string toolId )
{
    return std::make_shared<HostProcessAgentToolProxy>( std::move( entry ), std::move( pluginId ),
                                                        std::move( toolId ) );
}

std::shared_ptr<IPluginDataProviderV1> makeHostProcessDataProviderProxy(
    PluginHostSessionEntryPtr entry, std::string pluginId, std::string providerId )
{
    return std::make_shared<HostProcessDataProviderProxy>( std::move( entry ),
                                                           std::move( pluginId ),
                                                           std::move( providerId ) );
}

PluginModelRuntimePtrV1 makeHostProcessModelRuntimeProxy(
    PluginHostSessionEntryPtr entry, std::string pluginId, std::string framework,
    const PluginModelRequestV1 &request, std::string &error )
{
    return std::make_unique<HostProcessModelRuntimeProxy>( std::move( entry ),
                                                           std::move( pluginId ),
                                                           std::move( framework ), request,
                                                           error );
}

} // namespace sicnu::plugins
