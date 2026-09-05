/***************************************************************************
 * exprs/plugin_loader.cpp
 ***************************************************************************/
#include "exprs/plugin_loader.h"

#include <dlfcn.h>

#include <set>

#include "exprs/plugin_diagnostics.h"

namespace exprs {

namespace {

PluginDiagnostic makeError( PluginDiagnosticCode code, const std::string &pluginId,
                            const std::string &message, const std::string &field = {},
                            const std::string &file = {} )
{
    PluginDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = PluginDiagnosticSeverity::Error;
    diagnostic.pluginId = pluginId;
    diagnostic.message = message;
    diagnostic.field = field;
    diagnostic.file = file;
    return diagnostic;
}

/// Adapter that tags every contribution with the owning plugin id and
/// forwards into the sink. Also enforces: an id registered twice by the
/// same plugin is a RegistrationFailed error.
class SinkAdapter : public ContributionContextV1
{
public:
    SinkAdapter( const PluginManifest &manifest, const std::string &pluginId,
                 PluginContributionSink &sink, PluginDiagnosticLog &log )
        : mManifest( manifest )
        , mPluginId( pluginId )
        , mSink( sink )
        , mLog( log )
    {
    }

    bool registerOperatorFactory(
        const std::string &operatorId,
        std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory ) override
    {
        if ( !insert( operatorId ) )
            return false;
        if ( !mSink.registerOperatorFactory( mPluginId, operatorId, std::move( factory ) ) )
        {
            mLog.add( makeError( PluginDiagnosticCode::RegistrationFailed, mPluginId,
                                 "host rejected operator registration for '" + operatorId + "'",
                                 "operators" ) );
            return false;
        }
        return true;
    }

    bool registerDataProvider( const std::string &providerId,
                               std::shared_ptr<IPluginDataProviderV1> provider ) override
    {
        if ( !insert( providerId ) )
            return false;
        if ( !mSink.registerDataProvider( mPluginId, providerId, std::move( provider ) ) )
        {
            mLog.add( makeError( PluginDiagnosticCode::RegistrationFailed, mPluginId,
                                 "host rejected data provider registration for '" + providerId
                                     + "'",
                                 "data_providers" ) );
            return false;
        }
        return true;
    }

    bool registerModelRuntime( const std::string &framework,
                               PluginModelRuntimeFactoryV1 factory ) override
    {
        if ( !insert( framework ) )
            return false;
        if ( !mSink.registerModelRuntime( mPluginId, framework, std::move( factory ) ) )
        {
            mLog.add( makeError( PluginDiagnosticCode::RegistrationFailed, mPluginId,
                                 "host rejected model runtime registration for '" + framework
                                     + "'",
                                 "model_runtimes" ) );
            return false;
        }
        return true;
    }

    bool registerAgentTool( const std::string &toolId,
                            std::shared_ptr<IPluginAgentToolV1> tool ) override
    {
        if ( !insert( toolId ) )
            return false;
        if ( !mSink.registerAgentTool( mPluginId, toolId, std::move( tool ) ) )
        {
            mLog.add( makeError( PluginDiagnosticCode::RegistrationFailed, mPluginId,
                                 "host rejected agent tool registration for '" + toolId + "'",
                                 "agent_tools" ) );
            return false;
        }
        return true;
    }

    const PluginManifest &manifest() const override { return mManifest; }

private:
    bool insert( const std::string &id )
    {
        if ( id.empty() )
        {
            mLog.add( makeError( PluginDiagnosticCode::RegistrationFailed, mPluginId,
                                 "contribution registered with an empty id" ) );
            return false;
        }
        if ( !mSeenIds.insert( id ).second )
        {
            mLog.add( makeError( PluginDiagnosticCode::RegistrationFailed, mPluginId,
                                 "contribution id '" + id + "' registered twice", id ) );
            return false;
        }
        return true;
    }

    const PluginManifest &mManifest;
    std::string mPluginId;
    PluginContributionSink &mSink;
    PluginDiagnosticLog &mLog;
    std::set<std::string> mSeenIds;
};

/// Default host services: workspace/temp/data dirs resolved at construction.
class DefaultHostServices : public HostServicesV1
{
public:
    using HostLogSink = std::function<void( const char *, const std::string & )>;

    DefaultHostServices( std::string tempDirectory, std::string workspaceRoot,
                         std::string dataDirectory, HostLogSink sink )
        : mTempDirectory( std::move( tempDirectory ) )
        , mWorkspaceRoot( std::move( workspaceRoot ) )
        , mDataDirectory( std::move( dataDirectory ) )
        , mSink( std::move( sink ) )
    {
    }

    std::string tempDirectory() const override { return mTempDirectory; }
    std::string workspaceRoot() const override { return mWorkspaceRoot; }
    std::string dataDirectory() const override { return mDataDirectory; }
    void log( const char *level, const std::string &message ) override
    {
        if ( mSink )
            mSink( level, message );
    }

private:
    std::string mTempDirectory;
    std::string mWorkspaceRoot;
    std::string mDataDirectory;
    HostLogSink mSink;
};

/// Per-load forwarding facade: exposes the record's plugin directory to the
/// plugin via HostServicesV1::pluginDirectory().
class PluginServicesFacade : public HostServicesV1
{
public:
    PluginServicesFacade( HostServicesV1 &base, std::string pluginDirectory )
        : mBase( base )
        , mPluginDirectory( std::move( pluginDirectory ) )
    {
    }
    std::string tempDirectory() const override { return mBase.tempDirectory(); }
    std::string workspaceRoot() const override { return mBase.workspaceRoot(); }
    std::string dataDirectory() const override { return mBase.dataDirectory(); }
    void log( const char *level, const std::string &message ) override
    {
        mBase.log( level, message );
    }
    std::string pluginDirectory() const override { return mPluginDirectory; }

private:
    HostServicesV1 &mBase;
    std::string mPluginDirectory;
};

} // namespace

PluginLoader::~PluginLoader()
{
    if ( mLoaded.instance && mInstanceValid )
    {
        // Best effort: never throw from the destructor.
        try
        {
            mLoaded.instance->shutdown();
        }
        catch ( ... )
        {
        }
    }
    if ( mLoaded.libraryHandle )
        ::dlclose( mLoaded.libraryHandle );
}

bool PluginLoader::probeEntrypoint( const std::string &libraryPath, std::string &error )
{
    // NOTE: dlopen runs the library's ELF initializers — "probe" means no
    // entrypoint invocation, not zero code execution. The abi/API gate above
    // is the pre-execution boundary; see docs/plugins/isolation.md.
    void *handle = ::dlopen( libraryPath.c_str(), RTLD_LAZY | RTLD_LOCAL );
    if ( !handle )
    {
        const char *message = ::dlerror();
        error = message ? message : "dlopen failed";
        return false;
    }
    void *symbol = ::dlsym( handle, kPluginEntryPointV1 );
    const char *symbolError = ::dlerror();
    ::dlclose( handle );
    if ( !symbol )
    {
        error = "entrypoint symbol '" + std::string( kPluginEntryPointV1 ) + "' not found";
        return false;
    }
    return true;
}

bool PluginLoader::load( const PluginRecord &record, HostServicesV1 &services,
                         PluginContributionSink &sink, PluginDiagnosticLog &log )
{
    // The facade adds HostServicesV1::pluginDirectory() for this record.
    PluginServicesFacade servicesFacade( services, record.directory );
    mLoadedSink = &sink;
    return loadImpl( record, servicesFacade, sink, log );
}

bool PluginLoader::loadImpl( const PluginRecord &record, HostServicesV1 &services,
                             PluginContributionSink &sink, PluginDiagnosticLog &log )
{
    if ( mLoaded.instance )
    {
        log.add( makeError( PluginDiagnosticCode::LibraryLoadFailed, record.id(),
                            "loader instance reused without unload" ) );
        return false;
    }
    if ( record.manifest.entrypointKind != PluginEntrypointKind::Native
         || record.manifest.entrypoint.empty() )
    {
        log.add( makeError( PluginDiagnosticCode::EntrypointMissing, record.id(),
                            "plugin has no native entrypoint" ) );
        return false;
    }

    const std::string libraryPath = record.directory + "/" + record.manifest.entrypoint;
    void *handle = ::dlopen( libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL );
    if ( !handle )
    {
        const char *message = ::dlerror();
        log.add( makeError( PluginDiagnosticCode::LibraryLoadFailed, record.id(),
                            message ? message : "dlopen failed", "entrypoint", libraryPath ) );
        return false;
    }

    using CreateFn = PluginV1 *( * )();
    void *symbol = ::dlsym( handle, kPluginEntryPointV1 );
    const char *symbolError = ::dlerror();
    if ( !symbol || symbolError )
    {
        log.add( makeError( PluginDiagnosticCode::SymbolMissing, record.id(),
                            "entrypoint symbol '" + std::string( kPluginEntryPointV1 )
                                + "' not found",
                            kPluginEntryPointV1, libraryPath ) );
        ::dlclose( handle );
        return false;
    }

    PluginV1 *instance = nullptr;
    try
    {
        instance = reinterpret_cast<CreateFn>( symbol )();
    }
    catch ( const std::exception &exception )
    {
        log.add( makeError( PluginDiagnosticCode::LibraryLoadFailed, record.id(),
                            std::string( "entrypoint threw: " ) + exception.what() ) );
        ::dlclose( handle );
        return false;
    }
    catch ( ... )
    {
        log.add( makeError( PluginDiagnosticCode::LibraryLoadFailed, record.id(),
                            "entrypoint threw an unknown exception" ) );
        ::dlclose( handle );
        return false;
    }
    if ( !instance )
    {
        log.add( makeError( PluginDiagnosticCode::LibraryLoadFailed, record.id(),
                            "entrypoint returned nullptr" ) );
        ::dlclose( handle );
        return false;
    }

    if ( instance->pluginId() != record.manifest.id )
    {
        log.add( makeError( PluginDiagnosticCode::InitializationFailed, record.id(),
                            "plugin reports id '" + instance->pluginId()
                                + "' but the manifest declares '" + record.manifest.id + "'" ) );
        delete instance;
        ::dlclose( handle );
        return false;
    }

    mLoaded.pluginId = record.manifest.id;
    mLoaded.instance = instance;
    mLoaded.libraryHandle = handle;
    mLoaded.initialized = false;
    mLoaded.shutdownCalled = false;
    mInstanceValid = false;

    try
    {
        if ( !instance->initialize( services ) )
        {
            log.add( makeError( PluginDiagnosticCode::InitializationFailed, record.id(),
                                "plugin initialize() returned false" ) );
            delete instance;
            mLoaded = LoadedPlugin{};
            ::dlclose( handle );
            return false;
        }
        mLoaded.initialized = true;
        mInstanceValid = true;

        SinkAdapter adapter( record.manifest, record.manifest.id, sink, log );
        instance->registerContributions( adapter );

        // Optional UI contribution entry point (exprs/plugin_ui.h).
        // Failure here is a warning, not a load failure.
        if ( void *uiSymbol = ::dlsym( handle, kUiContributionEntryPointV1 ) )
        {
            using CreateUiFn = void *( * )();
            try
            {
                mLoaded.uiContribution = reinterpret_cast<CreateUiFn>( uiSymbol )();
            }
            catch ( ... )
            {
                mLoaded.uiContribution = nullptr;
            }
            if ( !mLoaded.uiContribution )
            {
                PluginDiagnostic warning;
                warning.code = PluginDiagnosticCode::RegistrationFailed;
                warning.severity = PluginDiagnosticSeverity::Warning;
                warning.pluginId = record.id();
                warning.message = "UI contribution entrypoint returned nullptr";
                log.add( warning );
            }
        }
    }
    catch ( const std::exception &exception )
    {
        log.add( makeError( PluginDiagnosticCode::InitializationFailed, record.id(),
                            std::string( "plugin threw during initialize/register: " )
                                + exception.what() ) );
        try
        {
            instance->shutdown();
        }
        catch ( ... )
        {
        }
        delete instance;
        mLoaded = LoadedPlugin{};
        ::dlclose( handle );
        return false;
    }
    return true;
}

bool PluginLoader::unload( LoadedPlugin &plugin, PluginDiagnosticLog &log )
{
    if ( !plugin.instance && !plugin.libraryHandle )
        return true;
    // Safety net (the registry does this itself before calling unload):
    // revoke contributions registered through the sink used at load time.
    if ( mLoadedSink )
    {
        mLoadedSink->revokePlugin( plugin.pluginId );
        mLoadedSink = nullptr;
    }
    if ( plugin.initialized && !plugin.shutdownCalled )
    {
        try
        {
            plugin.instance->shutdown();
        }
        catch ( const std::exception &exception )
        {
            log.add( makeError( PluginDiagnosticCode::InitializationFailed, plugin.pluginId,
                                std::string( "shutdown() threw: " ) + exception.what() ) );
        }
        plugin.shutdownCalled = true;
        mInstanceValid = false;
    }
    // The instance was allocated by the plugin (EXPRS_createPluginV1); delete
    // it while the library is still mapped, before dlclose.
    delete plugin.instance;
    plugin.instance = nullptr;
    const bool closed = ::dlclose( plugin.libraryHandle ) == 0;
    if ( !closed )
    {
        log.add( makeError( PluginDiagnosticCode::LibraryLoadFailed, plugin.pluginId,
                            "dlclose failed" ) );
    }
    plugin = LoadedPlugin{};
    mLoaded = LoadedPlugin{};
    return closed;
}

LoadedPlugin PluginLoader::take()
{
    LoadedPlugin out = mLoaded;
    mLoaded = LoadedPlugin{};
    mInstanceValid = false;
    return out;
}

void PluginLoader::markShutdownCalled( LoadedPlugin &plugin )
{
    plugin.shutdownCalled = true;
    if ( mLoaded.instance == plugin.instance )
        mInstanceValid = false;
}

std::unique_ptr<HostServicesV1> PluginLoader::createDefaultHostServices(
    std::string tempDirectory, std::string workspaceRoot, std::string dataDirectory,
    std::function<void( const char *, const std::string & )> logSink )
{
    return std::make_unique<DefaultHostServices>( std::move( tempDirectory ),
                                                  std::move( workspaceRoot ),
                                                  std::move( dataDirectory ), std::move( logSink ) );
}

} // namespace exprs
