#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/plugin_host.h"
#include "app/python/sicnu_app_interface.h"
#include "app/project_context.h"
#include "data/data_manager.h"
#include "python/qgis_python.h"
#include "python_ipc_server.h"
#include "python_plugin_host.h"
#include "python_worker_process_pool.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonObject>
#include <QTimer>
#include <qgsapplication.h>
#include <qgsproject.h>

static QString fixturePath( const QString &relPath )
{
    return QDir( QStringLiteral( TEST_DATA_DIR ) ).filePath( relPath );
}

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

TEST_CASE( "PluginHost loads plugins headlessly without GUI widgets", "[core][plugin_host]" )
{
#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
    REQUIRE( QgisPython::instance().initialize() );
#endif

    // Construct a headless ProjectContext (no display view, zero QWidget —
    // ADR 0023, TICKET-14) and a headless SicnuAppInterface.
    auto createdContext = sicnu::app::ProjectContext::createHeadless();
    REQUIRE( createdContext );
    std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

    SicnuAppInterface iface( nullptr, nullptr, context.get() );

    PluginHost host( 2 );
    host.setAppInterface( &iface );

    CHECK( host.appInterface() == &iface );
    CHECK( host.loadedPlugins().isEmpty() );

#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
    const QString pluginDir = fixturePath( QStringLiteral( "plugins" ) );
    host.loadPlugins( pluginDir );

    CHECK( host.isPluginLoaded( QStringLiteral( "Sample Python Plugin" ) ) );
    CHECK( host.loadedPlugins().contains( QStringLiteral( "Sample Python Plugin" ) ) );

    SicnuPluginInterface *plugin = host.plugin( QStringLiteral( "Sample Python Plugin" ) );
    REQUIRE( plugin != nullptr );
    CHECK( plugin->name() == QStringLiteral( "Sample Python Plugin" ) );
    CHECK( plugin->version() == QStringLiteral( "1.0" ) );

    // Execute a plugin command over IPC on the worker pool hosting the loaded
    // plugin (mirrors the sendRequestAndAwait pattern in test_python_plugin_host).
    auto *pythonHost = host.pythonPluginHost();
    REQUIRE( pythonHost != nullptr );
    REQUIRE( pythonHost->pool() != nullptr );
    // Workers connect back to the IPC server asynchronously — retry acquire
    // until one has a live client (mirrors test_python_plugin_manager).
    sicnu::python::isolated::WorkerNode *node = nullptr;
    for ( int attempt = 0; attempt < 100; ++attempt )
    {
        node = pythonHost->pool()->acquireWorker();
        if ( node )
            break;
        QEventLoop waitLoop;
        QTimer::singleShot( 100, &waitLoop, &QEventLoop::quit );
        waitLoop.exec();
    }
    REQUIRE( node != nullptr );
    REQUIRE( node->server != nullptr );

    QJsonObject regResult;
    bool regIsError = false;
    REQUIRE( node->server->sendRequestAndAwait( QStringLiteral( "processing.test_register_algorithm" ),
                                                QJsonObject(), regResult, regIsError, 10000 )
             == sicnu::python::isolated::AwaitStatus::Ok );
    REQUIRE( !regIsError );

    QJsonObject execParams;
    execParams[QStringLiteral( "id" )] = QStringLiteral( "py:echo_test" );
    execParams[QStringLiteral( "params" )] = QJsonObject{ { QStringLiteral( "value" ), 7 } };
    QJsonObject execResult;
    bool execIsError = false;
    REQUIRE( node->server->sendRequestAndAwait( QStringLiteral( "processing.execute_algorithm" ),
                                                execParams, execResult, execIsError, 10000 )
             == sicnu::python::isolated::AwaitStatus::Ok );
    CHECK( !execIsError );
    CHECK( execResult.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "ok" ) );
    CHECK( execResult.value( QStringLiteral( "result" ) ).toObject()
             .value( QStringLiteral( "echo" ) ).toObject()
             .value( QStringLiteral( "value" ) ).toInt() == 7 );

    pythonHost->pool()->releaseWorker( node );

    host.unloadAll();
    CHECK_FALSE( host.isPluginLoaded( QStringLiteral( "Sample Python Plugin" ) ) );
    CHECK( host.loadedPlugins().isEmpty() );
#endif
}
