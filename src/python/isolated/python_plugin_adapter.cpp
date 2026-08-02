// python_plugin_adapter.cpp — C++ adapter wrapping Python plugins for PluginManager
#include "python_plugin_adapter.h"
#include "python_app_interface_proxy.h"
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
                                          sicnu::data::DataManager *dataManager,
                                          QMenu *pluginMenu,
                                          ActiveViewHost *activeViewHost,
                                          PythonWorkerProcessPool *pool )
    : m_pluginDir( pluginDir )
    , m_packageName( packageName )
    , m_name( name )
    , m_description( description )
    , m_version( version )
    , m_dataManager( dataManager )
    , m_pluginMenu( pluginMenu )
    , m_activeViewHost( activeViewHost )
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

    // Attach UI RPC Proxy Facade (headless asset seam: DataManager is the
    // asset authority; menu and view host remain optional GUI enhancements).
    m_uiProxy = std::make_unique<PythonAppInterfaceProxy>( m_workerNode->server, m_dataManager, m_pluginMenu );
    if ( m_activeViewHost )
    {
        m_uiProxy->setActiveViewHost( m_activeViewHost );
    }

    // Send RPC load_plugin request
    QJsonObject params;
    params[QStringLiteral( "plugin_dir" )] = m_pluginDir;
    params[QStringLiteral( "package_name" )] = m_packageName;

    QEventLoop loop;
    bool success = false;
    QTimer timer;
    timer.setSingleShot( true );
    timer.setInterval( 5000 ); // 5 second timeout

    m_workerNode->server->sendRequest( QStringLiteral( "load_plugin" ), params, [&]( const QJsonObject &response, bool isError ) {
        if ( !isError && response.contains( QStringLiteral( "status" ) ) && response[QStringLiteral( "status" )].toString() == QStringLiteral( "loaded" ) )
        {
            success = true;
        }
        else if ( isError )
        {
            qWarning() << "PythonPluginAdapter load_plugin error:" << response;
        }
        loop.quit();
    } );

    QObject::connect( &timer, &QTimer::timeout, &loop, &QEventLoop::quit );
    timer.start();
    loop.exec();

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
    QTimer timer;
    timer.setSingleShot( true );
    timer.setInterval( 3000 );

    m_workerNode->server->sendRequest( QStringLiteral( "unload_plugin" ), params, [&]( const QJsonObject &, bool ) {
        loop.quit();
    } );

    QObject::connect( &timer, &QTimer::timeout, &loop, &QEventLoop::quit );
    timer.start();
    loop.exec();

    if ( m_uiProxy )
    {
        m_uiProxy.reset();
    }

    if ( m_pool && m_workerNode )
    {
        m_pool->releaseWorker( m_workerNode );
        m_workerNode = nullptr;
    }

    m_initialized = false;
}
