/***************************************************************************
 * src/plugins/host/plugin_host_process_runtime.cpp
 ***************************************************************************/
#include "plugin_host_process_runtime.h"

#include "plugin_host_protocol.h"
#include "plugin_host_proxies.h"

#include "exprs/ipc_frame.h"
#include "exprs/plugin_loader.h"

#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace exprs;
using namespace sicnu::plugins;
using namespace sicnu::plugins::hostprotocol;

namespace sicnu {
namespace plugins {

std::string defaultPluginHostWorkerPath()
{
    std::filesystem::path exePath;
#ifdef _WIN32
    char buffer[ MAX_PATH ];
    const DWORD size = ::GetModuleFileNameA( nullptr, buffer, MAX_PATH );
    if ( size > 0 && size < MAX_PATH )
        exePath = std::filesystem::path( buffer ).parent_path();
#else
    std::error_code ec;
    exePath = std::filesystem::read_symlink( "/proc/self/exe", ec ).parent_path();
    if ( ec )
        exePath.clear();
#endif
    if ( exePath.empty() )
        return {};
#ifdef _WIN32
    return ( exePath / "exprs_plugin_host_worker.exe" ).generic_string();
#else
    return ( exePath / "exprs_plugin_host_worker" ).generic_string();
#endif
}

} // namespace plugins
} // namespace sicnu

PluginHostProcessRuntime::PluginHostProcessRuntime( Options options )
    : mOptions( std::move( options ) )
{
    if ( mOptions.workerPath.empty() )
        mOptions.workerPath = defaultPluginHostWorkerPath();
}

bool PluginHostProcessRuntime::loadParamsFor( const PluginRecord &record,
                                              HostServicesV1 &services, Json::Value &params,
                                              PluginDiagnosticLog &log ) const
{
    (void)log;
    params = Json::Value( Json::objectValue );
    params["entrypoint"] = record.manifest.entrypoint;
    params["pluginDirectory"] = record.directory;
    params["manifest"] = record.manifest.toJson();
    Json::Value serviceValues( Json::objectValue );
    serviceValues["tempDirectory"] = services.tempDirectory();
    serviceValues["workspaceRoot"] = services.workspaceRoot();
    serviceValues["dataDirectory"] = services.dataDirectory();
    serviceValues["pluginDirectory"] = services.pluginDirectory();
    params["services"] = serviceValues;
    return true;
}

bool PluginHostProcessRuntime::loadPlugin( const PluginRecord &record, HostServicesV1 &services,
                                           PluginContributionSink &sink,
                                           PluginDiagnosticLog &log )
{
    const std::string pluginId = record.id();

    Json::Value params;
    if ( !loadParamsFor( record, services, params, log ) )
        return false;

    auto entry = std::make_shared<PluginHostSessionEntry>();
    PluginHostProcessSession::SpawnOptions spawnOptions;
    spawnOptions.workerPath = mOptions.workerPath;
    spawnOptions.pluginId = pluginId;
    spawnOptions.pluginDirectory = record.directory;
    // Manifest quotas clamped to host ceilings; a manifest can lower its own
    // limits but never raise them beyond policy.
    entry->quota = mOptions.quotaCeilings;
    {
        std::vector<std::string> warnings;
        entry->quota.parseManifest( record.manifest.quotas, warnings );
        for ( const std::string &warning : warnings )
            log.add( PluginDiagnosticCode::ManifestInvalidField, PluginDiagnosticSeverity::Warning,
                     warning, pluginId );
        entry->quota.clampTo( mOptions.quotaCeilings );
    }
    spawnOptions.quota = entry->quota;
    spawnOptions.handshakeTimeoutMs = mOptions.handshakeTimeoutMs;
    spawnOptions.killGraceMs = mOptions.killGraceMs;

    // Protocol 1.1 limits negotiation (params travel to the worker with
    // plugin.load): the frame cap lowers to the quota's maxResponseBytes
    // when it is below the transport default, and the dispatch width is
    // the quota's concurrency (the worker clamps to its own capability).
    {
        Json::Value limits( Json::objectValue );
        IpcFrameLimits transport;
        if ( entry->quota.maxResponseBytes > 0
             && static_cast<long long>( entry->quota.maxResponseBytes )
                    < static_cast<long long>( transport.maxFrameBytes ) )
            limits["maxFrameBytes"] = static_cast<Json::Int64>( entry->quota.maxResponseBytes );
        limits["maxConcurrentRequests"] = entry->quota.maxRequestConcurrency;
        params["limits"] = limits;
    }

    auto session = PluginHostProcessSession::spawn( spawnOptions, log );
    if ( !session )
    {
        log.add( PluginDiagnosticCode::HostProcessUnavailable, PluginDiagnosticSeverity::Error,
                 "could not launch the host-process worker", pluginId );
        return false;
    }
    entry->session = session;
    entry->loadParams = params;
    entry->entrypointPath = record.directory + "/" + record.manifest.entrypoint;
    entry->runtime = this;
    entry->pluginId = pluginId;

    IpcChannel::Outcome outcome =
        session->requestRaw( kLoadPlugin, params, mOptions.loadTimeoutMs );
    if ( outcome.status != IpcChannel::Outcome::Status::Ok )
    {
        log.add( PluginDiagnosticCode::LibraryLoadFailed, PluginDiagnosticSeverity::Error,
                 "worker plugin.load failed: " + outcome.error.message, pluginId );
        session->shutdown( 5000, log );
        return false;
    }

    // Register PROXY contributions from the worker's registration report.
    const Json::Value &registered = outcome.result["registered"];
    bool registrationsOk = true;
    for ( const Json::Value &operatorId : registered["operators"] )
    {
        auto factory = [entry, id = operatorId.asString()]()
            -> std::unique_ptr<sicnu::operators::RSOperator> {
            return makeHostProcessOperatorProxy( entry, entry->pluginId, id );
        };
        if ( !sink.registerOperatorFactory( pluginId, operatorId.asString(), factory ) )
            registrationsOk = false;
    }
    for ( const Json::Value &providerId : registered["dataProviders"] )
    {
        auto provider = makeHostProcessDataProviderProxy( entry, pluginId, providerId.asString() );
        if ( !sink.registerDataProvider( pluginId, providerId.asString(), provider ) )
            registrationsOk = false;
    }
    for ( const Json::Value &framework : registered["modelRuntimes"] )
    {
        auto factory = [entry, frameworkName = framework.asString()](
                           const PluginModelRequestV1 &request,
                           std::string &error ) -> PluginModelRuntimePtrV1 {
            return makeHostProcessModelRuntimeProxy( entry, entry->pluginId, frameworkName,
                                                     request, error );
        };
        if ( !sink.registerModelRuntime( pluginId, framework.asString(), factory ) )
            registrationsOk = false;
    }
    for ( const Json::Value &toolId : registered["agentTools"] )
    {
        auto tool = makeHostProcessAgentToolProxy( entry, pluginId, toolId.asString() );
        if ( !sink.registerAgentTool( pluginId, toolId.asString(), tool ) )
            registrationsOk = false;
    }
    if ( !registrationsOk )
    {
        log.add( PluginDiagnosticCode::RegistrationFailed, PluginDiagnosticSeverity::Error,
                 "one or more worker registrations were rejected by the sink", pluginId );
        session->shutdown( 5000, log );
        return false;
    }

    std::lock_guard<std::mutex> lock( mMutex );
    mSessions[ pluginId ] = entry;
    return true;
}

bool PluginHostProcessRuntime::respawn( const std::string &pluginId,
                                        PluginHostSessionEntry &entry, PluginDiagnosticLog &log )
{
    // Serializes concurrent recovery attempts; the proxy-side armed flag
    // already collapsed the stampede.
    std::lock_guard<std::mutex> lock( mMutex );

    // Restart policy: at most maxRestarts respawns inside a rolling
    // window; afterwards recovery refuses (typed E6005 at the caller)
    // until an unload/reload cycle resets the counter.
    {
        const auto now = std::chrono::steady_clock::now();
        if ( mRestartWindowArmed
             && std::chrono::duration_cast<std::chrono::milliseconds>( now
                                                                       - mFirstRestart )
                    .count()
                    > mOptions.restartWindowMs )
        {
            mRestartWindowArmed = false;
            mRestartCount = 0;
        }
        if ( !mRestartWindowArmed )
        {
            mRestartWindowArmed = true;
            mFirstRestart = now;
            mRestartCount = 0;
        }
        if ( mRestartCount >= mOptions.maxRestarts )
        {
            log.add( PluginDiagnosticCode::HostProcessCrashed,
                     PluginDiagnosticSeverity::Error,
                     "restart policy exhausted (" + std::to_string( mOptions.maxRestarts )
                         + " respawns in " + std::to_string( mOptions.restartWindowMs )
                         + " ms); unload/reload resets it",
                     pluginId );
            return false;
        }
        ++mRestartCount;
    }

    PluginHostProcessSession::SpawnOptions spawnOptions;
    spawnOptions.workerPath = mOptions.workerPath;
    spawnOptions.pluginId = pluginId;
    spawnOptions.quota = entry.quota;
    spawnOptions.handshakeTimeoutMs = mOptions.handshakeTimeoutMs;
    spawnOptions.killGraceMs = mOptions.killGraceMs;

    auto session = PluginHostProcessSession::spawn( spawnOptions, log );
    if ( !session )
        return false;
    IpcChannel::Outcome outcome =
        session->requestRaw( kLoadPlugin, entry.loadParams, mOptions.loadTimeoutMs );
    if ( outcome.status != IpcChannel::Outcome::Status::Ok )
    {
        log.add( PluginDiagnosticCode::HostProcessCrashed, PluginDiagnosticSeverity::Error,
                 "respawned worker failed to reload the plugin: " + outcome.error.message,
                 pluginId );
        session->shutdown( 5000, log );
        return false;
    }
    // Publish under entry.mutex: proxies call respawn OUTSIDE that lock
    // (a non-recursive double-lock would throw), so taking it here is
    // safe and gives readers a happens-before edge.
    {
        std::lock_guard<std::mutex> entryLock( entry.mutex );
        entry.session = session;
    }
    log.add( PluginDiagnosticCode::HostProcessCrashed, PluginDiagnosticSeverity::Info,
             "worker respawned and the plugin was reloaded (restart policy applied)", pluginId );
    return true;
}

bool PluginHostProcessRuntime::unloadPlugin( const std::string &pluginId,
                                             PluginDiagnosticLog &log )
{
    std::shared_ptr<PluginHostProcessSession> session;
    {
        std::lock_guard<std::mutex> lock( mMutex );
        auto iterator = mSessions.find( pluginId );
        if ( iterator == mSessions.end() )
            return true; // nothing hosted (failed load) — nothing to tear down
        session = iterator->second->session;
        mSessions.erase( iterator );
    }
    return session->shutdown( 10000, log );
}

Json::Value PluginHostProcessRuntime::diagnosticsSnapshot() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    Json::Value snapshot( Json::objectValue );
    snapshot["protocolVersion"] = EXP_RS_HOST_PROTOCOL_VERSION;
    snapshot["workerPath"] = mOptions.workerPath;
    Json::Value plugins( Json::objectValue );
    for ( const auto &[ pluginId, entry ] : mSessions )
    {
        Json::Value entryJson( Json::objectValue );
        entryJson["workerAlive"] = entry->session->isAlive();
        entryJson["generation"] = entry->session->generation();
        entryJson["poisoned"] = entry->session->isPoisoned();
        entryJson["effectiveConcurrency"] = entry->session->effectiveConcurrency();
        entryJson["quota"] = entry->quota.toJson();
        plugins[ pluginId ] = entryJson;
    }
    snapshot["plugins"] = plugins;
    return snapshot;
}

Json::Value PluginHostProcessRuntime::describeUiSchema( const std::string &pluginId,
                                                        PluginDiagnosticLog &log )
{
    Json::Value result( Json::objectValue );
    std::shared_ptr<PluginHostProcessSession> session;
    {
        std::lock_guard<std::mutex> lock( mMutex );
        auto iterator = mSessions.find( pluginId );
        if ( iterator == mSessions.end() )
        {
            result["ok"] = false;
            result["error"] = "plugin is not hosted (E4003)";
            return result;
        }
        session = iterator->second->session;
    }
    if ( !session || !session->isAlive() )
    {
        result["ok"] = false;
        result["error"] = "worker process is not running (E6005)";
        return result;
    }
    IpcChannel::Outcome outcome =
        session->request( kDescribeUi, Json::Value( Json::objectValue ),
                          std::max( 10000, mOptions.handshakeTimeoutMs ) );
    if ( outcome.status == IpcChannel::Outcome::Status::Error
         && outcome.error.code == "E6008" )
    {
        result["ok"] = false;
        result["error"] = "plugin provides no declarative UI (E6008)";
        return result;
    }
    if ( outcome.status != IpcChannel::Outcome::Status::Ok )
    {
        result["ok"] = false;
        result["error"] = outcome.error.message.empty()
                              ? "ui.describe failed"
                              : outcome.error.message;
        if ( !outcome.error.code.empty() )
        {
            result["code"] = outcome.error.code;
            result["error"] = result["error"].asString() + " (" + outcome.error.code + ")";
        }
        log.add( PluginDiagnosticCode::IpcProtocolError, PluginDiagnosticSeverity::Warning,
                 "ui.describe failed: " + outcome.error.message, pluginId );
        return result;
    }
    result["ok"] = true;
    result["schema"] = outcome.result["schema"];
    return result;
}

Json::Value PluginHostProcessRuntime::invokeUi( const std::string &pluginId,
                                                const Json::Value &event, int timeoutMs,
                                                PluginDiagnosticLog &log )
{
    Json::Value result( Json::objectValue );
    std::shared_ptr<PluginHostProcessSession> session;
    {
        std::lock_guard<std::mutex> lock( mMutex );
        auto iterator = mSessions.find( pluginId );
        if ( iterator == mSessions.end() )
        {
            result["ok"] = false;
            result["error"] = "plugin is not hosted (E4003)";
            return result;
        }
        session = iterator->second->session;
    }
    if ( !session || !session->isAlive() )
    {
        result["ok"] = false;
        result["error"] = "worker process is not running (E6005)";
        return result;
    }
    Json::Value params( Json::objectValue );
    params["event"] = event;
    IpcChannel::Outcome outcome =
        session->request( kInvokeUi, params, timeoutMs > 0 ? timeoutMs : 10000 );
    if ( outcome.status != IpcChannel::Outcome::Status::Ok )
    {
        result["ok"] = false;
        result["error"] = outcome.error.message.empty() ? "ui.invoke failed"
                                                        : outcome.error.message;
        if ( !outcome.error.code.empty() )
            result["code"] = outcome.error.code;
        return result;
    }
    result["ok"] = true;
    result["response"] = outcome.result["response"];
    return result;
}

bool PluginHostProcessRuntime::isWorkerAlive( const std::string &pluginId ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    auto iterator = mSessions.find( pluginId );
    return iterator != mSessions.end() && iterator->second->session->isAlive();
}
