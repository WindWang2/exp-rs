#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "python/qgis_python.h"
#include "python/sicnu_python_api.h"
#include "python/sicnu_python_runner.h"
#include "python/sicnu_app_interface.h"
#include "python/python_plugin_adapter.h"
#include "python_ipc_server.h"
#include "python_worker_process.h"
#include "shared_memory_segment.h"
#include "python_app_interface_proxy.h"
#include "python_worker_process_pool.h"
#include "plugin_manager.h"

#include <QEventLoop>
#include <QTimer>

#include "active_view_host.h"
#include "project_context.h"

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgslayertreeview.h>
#include <qgsmessagebar.h>

#include <QMainWindow>
#include <QDir>
#include <QFileInfo>

static QString fixturePath( const QString &relativePath )
{
  return QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( relativePath );
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

TEST_CASE( "SicnuAppInterface implements QgisInterface facade", "[python][iface]" )
{
  REQUIRE( QgisPython::instance().initialize() );

  QMainWindow mainWindow;
  QgsMapCanvas canvas;
  QgsMessageBar messageBar;

  SicnuAppInterface iface( &mainWindow, nullptr, nullptr, &canvas, &messageBar );

  CHECK( iface.mainWindow() == &mainWindow );
  CHECK( iface.mapCanvas() == &canvas );
  CHECK( iface.messageBar() == &messageBar );
  CHECK( iface.pluginMenu() != nullptr );
  CHECK( iface.pluginToolBar() != nullptr );

  QAction testAction( QStringLiteral( "Test Plugin Action" ), &mainWindow );
  iface.addPluginToMenu( QStringLiteral( "Test Submenu" ), &testAction );
  CHECK( iface.addToolBarIcon( &testAction ) == 0 );
}

TEST_CASE( "PluginManager scans and loads Python plugin directory", "[python][plugin_manager]" )
{
  REQUIRE( QgisPython::instance().initialize() );

  QMainWindow mainWindow;
  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  QgsMessageBar messageBar;

  SicnuAppInterface iface( &mainWindow, nullptr, nullptr, &canvas, &messageBar );
  PluginManager manager( &canvas, &treeView );
  manager.setAppInterface( &iface );

  const QString pluginDir = fixturePath( QStringLiteral( "plugins" ) );
  manager.loadPlugins( pluginDir );

  CHECK( manager.isPluginLoaded( QStringLiteral( "Sample Python Plugin" ) ) );
  CHECK( manager.loadedPlugins().contains( QStringLiteral( "Sample Python Plugin" ) ) );

  SicnuPluginInterface *plugin = manager.plugin( QStringLiteral( "Sample Python Plugin" ) );
  REQUIRE( plugin != nullptr );
  CHECK( plugin->name() == QStringLiteral( "Sample Python Plugin" ) );
  CHECK( plugin->version() == QStringLiteral( "1.0" ) );

  manager.unloadAll();
  CHECK_FALSE( manager.isPluginLoaded( QStringLiteral( "Sample Python Plugin" ) ) );
}

TEST_CASE( "PythonWorkerProcess & PythonIpcServer start subprocess and achieve Ping/Pong handshake", "[python][isolated]" )
{
  using namespace sicnu::python::isolated;

  PythonIpcServer server;
  QString socketName = QString( "sicnu_py_test_%1" ).arg( QCoreApplication::applicationPid() );

  REQUIRE( server.listen( socketName ) );
  REQUIRE( server.isListening() );

  PythonWorkerProcess worker;
  QString scriptPath = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) );

  REQUIRE( worker.startWorker( socketName, QString(), scriptPath ) );
  REQUIRE( worker.isRunning() );

  // Wait for client connection
  QEventLoop loop;
  QObject::connect( &server, &PythonIpcServer::clientConnected, &loop, &QEventLoop::quit );
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();

  // Send ping request
  bool receivedPong = false;
  QJsonObject pingParams;
  server.sendRequest( QStringLiteral( "ping" ), pingParams, [&]( const QJsonObject &result, bool isErr ) {
    if ( !isErr && result[QStringLiteral( "status" )].toString() == QStringLiteral( "pong" ) )
    {
      receivedPong = true;
    }
    loop.quit();
  } );

  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();

  CHECK( receivedPong );

  worker.stopWorker();
  server.close();
}

TEST_CASE( "SharedMemorySegment transfers 10MB raster matrix with zero-copy to Python worker", "[python][isolated][shm]" )
{
  using namespace sicnu::python::isolated;

  PythonIpcServer server;
  QString socketName = QString( "sicnu_py_shm_%1" ).arg( QCoreApplication::applicationPid() );

  REQUIRE( server.listen( socketName ) );

  PythonWorkerProcess worker;
  QString scriptPath = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) );

  REQUIRE( worker.startWorker( socketName, QString(), scriptPath ) );

  QEventLoop loop;
  QObject::connect( &server, &PythonIpcServer::clientConnected, &loop, &QEventLoop::quit );
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();

  SharedMemorySegment shm;
  QString shmKey = QString( "sicnu_shm_test_%1" ).arg( QCoreApplication::applicationPid() );
  size_t elementCount = 2500000; // 10 MB of floats
  size_t bytes = elementCount * sizeof( float );

  REQUIRE( shm.create( shmKey, bytes, 2500, 1000, 1, 0 ) );
  REQUIRE( shm.isAttached() );

  float *floatPtr = static_cast<float *>( shm.payload() );
  for ( size_t i = 0; i < 100; ++i )
  {
    floatPtr[i] = static_cast<float>( i + 1 );
  }

  bool shmProcessed = false;
  QJsonObject params;
  params[QStringLiteral( "shm_key" )] = shmKey;
  params[QStringLiteral( "multiply" )] = 2.0;

  server.sendRequest( QStringLiteral( "shm_process" ), params, [&]( const QJsonObject &result, bool isErr ) {
    if ( !isErr && result[QStringLiteral( "status" )].toString() == QStringLiteral( "success" ) )
    {
      shmProcessed = true;
    }
    loop.quit();
  } );

  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();

  CHECK( shmProcessed );
  // Verify Python modified data in place (zero copy!)
  CHECK( floatPtr[0] == 2.0f );
  CHECK( floatPtr[1] == 4.0f );
  CHECK( floatPtr[99] == 200.0f );

  worker.stopWorker();
  server.close();
}

TEST_CASE( "PythonAppInterfaceProxy registers UI action over IPC and routes click callbacks", "[python][isolated][ui]" )
{
  using namespace sicnu::python::isolated;

  PythonIpcServer server;
  QString socketName = QString( "sicnu_py_ui_%1" ).arg( QCoreApplication::applicationPid() );

  REQUIRE( server.listen( socketName ) );

  QMenu parentMenu;
  PythonAppInterfaceProxy uiProxy( &server, &parentMenu );

  PythonWorkerProcess worker;
  QString scriptPath = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) );

  REQUIRE( worker.startWorker( socketName, QString(), scriptPath ) );

  QEventLoop loop;
  QObject::connect( &server, &PythonIpcServer::clientConnected, &loop, &QEventLoop::quit );
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();

  // Ask Python worker to register a test UI action
  bool actionRegistered = false;
  QJsonObject params;
  params[QStringLiteral( "callback_id" )] = QStringLiteral( "cb_ui_test_001" );

  server.sendRequest( QStringLiteral( "ui.test_register_action" ), params, [&]( const QJsonObject &result, bool isErr ) {
    if ( !isErr )
    {
      actionRegistered = true;
    }
    loop.quit();
  } );

  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();

  CHECK( actionRegistered );
  CHECK( uiProxy.registeredActionCount() == 1 );
  CHECK( parentMenu.actions().size() == 1 );

  // Trigger C++ action click to verify RPC callback to Python
  bool callbackTriggered = false;
  QObject::connect( &uiProxy, &PythonAppInterfaceProxy::actionTriggered, [&]( const QString &cbId ) {
    if ( cbId == QStringLiteral( "cb_ui_test_001" ) )
    {
      callbackTriggered = true;
    }
  } );

  QAction *action = parentMenu.actions().first();
  action->trigger();

  CHECK( callbackTriggered );

  worker.stopWorker();
  server.close();
}

TEST_CASE( "PythonWorkerProcessPool detects worker crash and auto-heals pre-warmed worker process", "[python][isolated][fault]" )
{
  using namespace sicnu::python::isolated;

  QString scriptPath = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) );

  PythonWorkerProcessPool pool( 2 );
  REQUIRE( pool.initialize( QString(), scriptPath ) );

  QEventLoop loop;
  QTimer::singleShot( 500, &loop, &QEventLoop::quit );
  loop.exec();

  WorkerNode *node = nullptr;
  for ( int i = 0; i < 50; ++i )
  {
    node = pool.acquireWorker();
    if ( node )
      break;
    QEventLoop waitLoop;
    QTimer::singleShot( 100, &waitLoop, &QEventLoop::quit );
    waitLoop.exec();
  }
  REQUIRE( node != nullptr );
  REQUIRE( node->server != nullptr );
  REQUIRE( node->server->hasClient() );

  int targetId = node->id;
  bool crashDetected = false;
  bool autoHealed = false;

  QEventLoop faultLoop;

  QObject::connect( &pool, &PythonWorkerProcessPool::workerCrashed, [&]( int id, const QString &reason ) {
    Q_UNUSED( reason );
    if ( id == targetId )
    {
      crashDetected = true;
    }
  } );

  QObject::connect( &pool, &PythonWorkerProcessPool::workerRestarted, [&]( int id ) {
    if ( id == targetId )
    {
      autoHealed = true;
      faultLoop.quit();
    }
  } );

  // Send crash_test command to simulate crash/segfault
  QJsonObject crashParams;
  node->server->sendRequest( QStringLiteral( "crash_test" ), crashParams );

  QTimer::singleShot( 6000, &faultLoop, &QEventLoop::quit );
  faultLoop.exec();

  CHECK( crashDetected );
  CHECK( autoHealed );
  CHECK( pool.activeWorkerCount() == 2 );

  pool.disconnect();
  pool.shutdown();
}

TEST_CASE( "PythonPluginAdapter initializes and unloads Python plugin over PythonWorkerProcessPool out-of-process IPC", "[python][adapter][isolated]" )
{
  using namespace sicnu::python::isolated;

  QString scriptPath = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) );

  PythonWorkerProcessPool pool( 2 );
  REQUIRE( pool.initialize( QString(), scriptPath ) );

  QEventLoop loop;
  QTimer::singleShot( 500, &loop, &QEventLoop::quit );
  loop.exec();

  QMainWindow mainWindow;
  QgsMapCanvas canvas;
  QgsMessageBar messageBar;

  SicnuAppInterface iface( &mainWindow, nullptr, nullptr, &canvas, &messageBar );

  const QString pluginDir = fixturePath( QStringLiteral( "plugins/sample_plugin" ) );
  PythonPluginAdapter adapter( pluginDir,
                               QStringLiteral( "sample_plugin" ),
                               QStringLiteral( "Sample Python Plugin" ),
                               QStringLiteral( "Sample Description" ),
                               QStringLiteral( "1.0" ),
                               &iface,
                               &pool );

  REQUIRE( adapter.initialize( &canvas, nullptr ) );

  // Unload plugin
  adapter.unload();

  pool.disconnect();
  pool.shutdown();
}

