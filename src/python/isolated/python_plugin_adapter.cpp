// python_plugin_adapter.cpp — C++ adapter wrapping Python plugins for PluginManager
#include "python_plugin_adapter.h"
#include "app_interface_bridge.h"
#include "python_worker_process_pool.h"

#include "app/project_context.h"
#include "app/python/sicnu_app_interface.h"
#include "data/data_manager.h"

#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonObject>
#include <QTimer>

using namespace sicnu::python::isolated;

PythonPluginAdapter::PythonPluginAdapter( const QString &pluginDir,
                                          const QString &packageName,
                                          const QString &name,
                                          const QString &description,
                                          const QString &version,
                                          const PluginLoadContext &context,
                                          PythonWorkerProcessPool *pool )
    : m_pluginDir( pluginDir )
    , m_packageName( packageName )
    , m_name( name )
    , m_description( description )
    , m_version( version )
    , m_dataManager( context.dataManager )
    , m_pluginMenu( context.pluginMenu )
    , m_activeViewHost( context.activeViewHost )
    , m_pool( pool )
{
    const QString iconPath = QDir( pluginDir ).filePath( QStringLiteral( "icon.png" ) );
    if ( QFileInfo::exists( iconPath ) )
    {
        m_icon = QIcon( iconPath );
    }
}

PythonPluginAdapter::~PythonPluginAdapter()
{
    if ( m_initialized )
    {
        unload();
    }
    // Belt and braces: unload() early-returns when the plugin was never fully
    // initialized, but the crash-logger connection may still be alive (#522).
    QObject::disconnect( m_workerCrashedConnection );
}

bool PythonPluginAdapter::initialize( SicnuAppInterface *iface )
{
    if ( iface )
    {
        if ( !m_dataManager && iface->projectContext() )
        {
            m_dataManager = &iface->projectContext()->dataManager();
        }
        if ( !m_pluginMenu )
        {
            m_pluginMenu = iface->pluginMenu();
        }
        if ( !m_activeViewHost )
        {
            m_activeViewHost = iface->activeViewHost();
        }
    }

    if ( m_initialized )
        return true;

    if ( !m_pool )
    {
        qWarning() << "PythonPluginAdapter: No PythonWorkerProcessPool provided";
        return false;
    }

    m_workerNode = m_pool->acquireWorker();
    if ( !m_workerNode )
    {
        QEventLoop waitLoop;
        QTimer waitTimer;
        waitTimer.setSingleShot( true );
        waitTimer.setInterval( 3000 );

        QTimer pollTimer;
        pollTimer.setInterval( 50 );
        QObject::connect( &pollTimer, &QTimer::timeout, [&]() {
            m_workerNode = m_pool->acquireWorker();
            if ( m_workerNode )
            {
                waitLoop.quit();
            }
        } );
        QObject::connect( &waitTimer, &QTimer::timeout, &waitLoop, &QEventLoop::quit );
        pollTimer.start();
        waitTimer.start();
        waitLoop.exec();
    }

    if ( !m_workerNode || !m_workerNode->server )
    {
        qWarning() << "PythonPluginAdapter: Failed to acquire worker process from pool";
        return false;
    }

    // Attach AppInterfaceBridge (headless asset seam: DataManager is the
    // asset authority; menu and view host remain optional GUI enhancements).
    m_bridge = std::make_unique<AppInterfaceBridge>( m_dataManager, m_activeViewHost, m_pluginMenu );
    m_bridge->bindIpcServer( m_workerNode->server );

    // A worker crash replaces the node's server; re-bind the bridge so the
    // IPC channel stays alive after the pool's self-healing restart
    // (ADR 0064). Plugin-side state is lost with the daemon and must be
    // reloaded by the caller. The adapter is not a QObject, so the bridge is
    // captured through a QPointer (no-op after unload) and the node id is
    // matched; the connection is severed in unload().
    m_restartRebindConnection = QObject::connect(
        m_pool, &PythonWorkerProcessPool::workerRestarted, m_pool,
        [node = m_workerNode, bridgePtr = QPointer<AppInterfaceBridge>( m_bridge.get() )]( int id ) {
            if ( node && node->server && bridgePtr && node->id == id )
                bridgePtr->bindIpcServer( node->server );
        } );
    // DATAPY-3: if the worker dies while this plugin owns the node, the pool
    // keeps isBusy=true (reserved). Listen for workerCrashed to avoid re-using
    // a stale server pointer; the next RPC will fail and trigger re-acquire via
    // the normal error path. This prevents crossed IPC where a second plugin
    // binds to the same server. The adapter is not a QObject: store the
    // connection and sever it in unload()/#dtor, otherwise the lambda's
    // captured this dangles once the adapter is destroyed (#522).
    m_workerCrashedConnection = QObject::connect( m_pool, &PythonWorkerProcessPool::workerCrashed, m_pool,
                      [this]( int id ) {
                          if ( m_workerNode && m_workerNode->id == id )
                          {
                              // Bridge stays bound to the old server until
                              // workerRestarted re-binds; no extra action needed
                              // beyond keeping isBusy reserved (pool side).
                              qWarning() << "PythonPluginAdapter: worker crashed for plugin"
                                         << m_packageName << "node" << id;
                          }
                      } );

    // Send RPC load_plugin request
    QJsonObject params;
    params[QStringLiteral( "plugin_dir" )] = m_pluginDir;
    params[QStringLiteral( "package_name" )] = m_packageName;

    QEventLoop loop;
    auto successState = std::make_shared<bool>( false );
    QPointer<QEventLoop> loopPtr( &loop );
    QTimer timer;
    timer.setSingleShot( true );
    timer.setInterval( 5000 ); // 5 second timeout

    // The callback outlives this frame when the pool replays it after a worker
    // crash (ADR 0064 recovery), so it must not capture stack locals: the
    // result lives on the heap and the loop is accessed through a QPointer.
    m_workerNode->server->sendRequest( QStringLiteral( "load_plugin" ), params,
      [successState, loopPtr]( const QJsonObject &response, bool isError ) {
        if ( !isError && response.contains( QStringLiteral( "status" ) ) && response[QStringLiteral( "status" )].toString() == QStringLiteral( "loaded" ) )
        {
            *successState = true;
        }
        else if ( isError )
        {
            qWarning() << "PythonPluginAdapter load_plugin error:" << response;
        }
        if ( loopPtr )
            loopPtr->quit();
      } );

    QObject::connect( &timer, &QTimer::timeout, &loop, &QEventLoop::quit );
    timer.start();
    loop.exec();

    const bool success = *successState;
    if ( !success )
    {
        qWarning() << "PythonPluginAdapter: Timed out or failed to load plugin:" << m_packageName;
        if ( m_pool && m_workerNode )
        {
            m_pool->releaseWorker( m_workerNode );
            m_workerNode = nullptr;
        }
        return false;
    }

    m_initialized = true;
    return true;
}

void PythonPluginAdapter::unload()
{
    if ( !m_initialized || !m_workerNode || !m_workerNode->server )
        return;

    QJsonObject params;
    params[QStringLiteral( "package_name" )] = m_packageName;

    QEventLoop loop;
    QPointer<QEventLoop> loopPtr( &loop );
    QTimer timer;
    timer.setSingleShot( true );
    timer.setInterval( 3000 );

    // Durable capture: may be replayed after a worker crash (ADR 0064).
    m_workerNode->server->sendRequest( QStringLiteral( "unload_plugin" ), params, [loopPtr]( const QJsonObject &, bool ) {
        if ( loopPtr )
            loopPtr->quit();
    } );

    QObject::connect( &timer, &QTimer::timeout, &loop, &QEventLoop::quit );
    timer.start();
    loop.exec();

    QObject::disconnect( m_restartRebindConnection );
    QObject::disconnect( m_workerCrashedConnection );
    if ( m_bridge )
    {
        m_bridge.reset();
    }

    if ( m_pool && m_workerNode )
    {
        m_pool->releaseWorker( m_workerNode );
        m_workerNode = nullptr;
    }

    m_initialized = false;
}
