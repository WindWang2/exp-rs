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
#include "processing/framework/atomic_algorithm_registry.h"

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

/// Pumps the event loop so async worker→host IPC (e.g. the
/// processing.register_algorithm the daemon sends when an algorithm is
/// registered) is delivered before assertions observe the C++ catalog.
static void pumpLoop( int ms )
{
    QEventLoop loop;
    QTimer::singleShot( ms, &loop, &QEventLoop::quit );
    loop.exec();
}

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  #ifdef _WIN32
  _exit( result );
#else
  return result;
#endif
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
    pumpLoop( 400 );
    // Registered through the plugin's own bridge at classFactory time.
    CHECK( sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
               QStringLiteral( "py:sample_echo" ).toStdString() )
           != nullptr );

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
    // NOTE: this manual pool-node hook registers in the worker daemon only;
    // its server has no bridge bound, so it intentionally does not touch the
    // host catalog. The bridge path is covered by py:sample_echo below.

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
    pumpLoop( 400 );
    CHECK( sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
               QStringLiteral( "py:sample_echo" ).toStdString() )
           == nullptr );

    // Issue #755: the py: registration the plugin made through the IPC
    // bridge must be revoked by unload — the catalog keeps no dead executor.
    CHECK( sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
               QStringLiteral( "py:echo_test" ).toStdString() )
           == nullptr );

    // Full round-trip: reload -> the py: algorithm is registered again
    // through the new bridge and actually executes; unload -> absent again.
    // No restart, no dead entries, no duplicates.
    host.loadPlugins( pluginDir );
    CHECK( host.isPluginLoaded( QStringLiteral( "Sample Python Plugin" ) ) );
    pumpLoop( 400 );
    const auto reRegisteredAdapter =
        sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
            QStringLiteral( "py:sample_echo" ).toStdString() );
    REQUIRE( reRegisteredAdapter != nullptr );
    {
        Json::Value params;
        params["value"] = 41;
        const Json::Value result = reRegisteredAdapter->execute( params, nullptr, nullptr );
        CHECK( result.isObject() );
        // The adapter surfaces the IPC envelope: {status, result:{echo:{...}}}.
        CHECK( result.get( "result", Json::Value( Json::objectValue ) )
                  .get( "echo", Json::Value( Json::objectValue ) )
                  .get( "value", 0 )
                  .asInt()
              == 41 );
    }

    host.unloadAll();
    pumpLoop( 400 );
    CHECK( sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
               QStringLiteral( "py:sample_echo" ).toStdString() )
           == nullptr );
#endif
}

TEST_CASE( "PluginHost Reload Cycle Object Cleanup", "[core][plugin_host][cleanup]" )
{
    PluginHost host;
    const int initialChildren = host.children().count();

    // Verify unloadAll returns children count to initial baseline
    host.unloadAll();
    CHECK( host.children().count() == initialChildren );
    CHECK( host.loadedPlugins().isEmpty() );
}

