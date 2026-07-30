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
#include "app_interface_bridge.h"
#include "python_app_interface_proxy.h"
#include "python_worker_process_pool.h"
#include "plugin_manager.h"
#include "processing/framework/algorithm_engine.h"

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
  ActiveViewHost host( &canvas, nullptr, nullptr, nullptr, nullptr,
                       sicnu::display::DisplayViewId(), &mainWindow );
  host.setMessageBar( &messageBar );

  SicnuAppInterface iface( &mainWindow, &host, nullptr );

  CHECK( iface.mainWindow() == &mainWindow );
  CHECK( iface.mapCanvas() == &canvas );
  CHECK( iface.messageBar() == &messageBar );
  CHECK( iface.activeViewHost() == &host );
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
  ActiveViewHost host( &canvas, &treeView, nullptr, nullptr, nullptr,
                       sicnu::display::DisplayViewId(), &mainWindow );
  host.setMessageBar( &messageBar );

  SicnuAppInterface iface( &mainWindow, &host, nullptr );
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
  ActiveViewHost host( &canvas, nullptr, nullptr, nullptr, nullptr,
                       sicnu::display::DisplayViewId(), &mainWindow );
  host.setMessageBar( &messageBar );

  SicnuAppInterface iface( &mainWindow, &host, nullptr );

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

TEST_CASE( "PythonAppInterfaceProxy handles catalog.get_active_layer, canvas.get_state, ui.push_message_bar over IPC", "[python][isolated][api]" )
{
  using namespace sicnu::python::isolated;

  PythonIpcServer server;
  QMenu parentMenu;
  QgsMapCanvas canvas;
  QgsMessageBar messageBar;
  ActiveViewHost activeViewHost( &canvas, nullptr, nullptr, nullptr, nullptr, sicnu::display::DisplayViewId(), nullptr );
  activeViewHost.setMessageBar( &messageBar );

  PythonAppInterfaceProxy uiProxy( &server, &parentMenu, &activeViewHost );

  // Test canvas.get_state IPC message handling
  QJsonObject canvasMsg;
  canvasMsg[QStringLiteral( "method" )] = QStringLiteral( "canvas.get_state" );
  canvasMsg[QStringLiteral( "id" )] = 101;
  uiProxy.handleIpcMessage( canvasMsg );

  // Test ui.push_message_bar IPC message handling (string + int level passthrough)
  QJsonObject msgBarMsg;
  msgBarMsg[QStringLiteral( "method" )] = QStringLiteral( "ui.push_message_bar" );
  msgBarMsg[QStringLiteral( "id" )] = 102;
  QJsonObject msgParams;
  msgParams[QStringLiteral( "title" )] = QStringLiteral( "Test Title" );
  msgParams[QStringLiteral( "text" )] = QStringLiteral( "Test Text" );
  msgParams[QStringLiteral( "level" )] = QStringLiteral( "warning" );
  msgBarMsg[QStringLiteral( "params" )] = msgParams;
  uiProxy.handleIpcMessage( msgBarMsg );

  QJsonObject msgBarMsgInt;
  msgBarMsgInt[QStringLiteral( "method" )] = QStringLiteral( "ui.push_message_bar" );
  msgBarMsgInt[QStringLiteral( "id" )] = 105;
  QJsonObject msgParamsInt;
  msgParamsInt[QStringLiteral( "title" )] = QStringLiteral( "Critical Title" );
  msgParamsInt[QStringLiteral( "text" )] = QStringLiteral( "Critical Text" );
  msgParamsInt[QStringLiteral( "level" )] = static_cast<int>( Qgis::MessageLevel::Critical );
  msgBarMsgInt[QStringLiteral( "params" )] = msgParamsInt;
  uiProxy.handleIpcMessage( msgBarMsgInt );

  // Test catalog.get_active_layer IPC message handling
  QJsonObject layerMsg;
  layerMsg[QStringLiteral( "method" )] = QStringLiteral( "catalog.get_active_layer" );
  layerMsg[QStringLiteral( "id" )] = 103;
  uiProxy.handleIpcMessage( layerMsg );

  // Test processing.register_algorithm IPC message handling
  QJsonObject regAlgoMsg;
  regAlgoMsg[QStringLiteral( "method" )] = QStringLiteral( "processing.register_algorithm" );
  regAlgoMsg[QStringLiteral( "id" )] = 104;
  QJsonObject algoParams;
  algoParams[QStringLiteral( "id" )] = QStringLiteral( "py:test_ndvi" );
  algoParams[QStringLiteral( "name" )] = QStringLiteral( "Test Python NDVI" );
  algoParams[QStringLiteral( "group" )] = QStringLiteral( "Python Plugins" );
  regAlgoMsg[QStringLiteral( "params" )] = algoParams;
  uiProxy.handleIpcMessage( regAlgoMsg );

  auto foundAlgo = sicnu::AlgorithmEngine::instance().findAlgorithm( QStringLiteral( "py:test_ndvi" ) );
  CHECK( foundAlgo != nullptr );
  if ( foundAlgo )
  {
    CHECK( foundAlgo->descriptor().name == QStringLiteral( "Test Python NDVI" ) );
  }

  CHECK( uiProxy.registeredActionCount() == 0 );
}

TEST_CASE( "AppInterfaceBridge consolidates QGIS query routing and JSON serialization", "[python][bridge]" )
{
  sicnu::python::isolated::AppInterfaceBridge bridge( nullptr );

  SECTION( "Null ActiveViewHost produces clean fallback summaries" )
  {
    auto layerSummary = bridge.getActiveLayerSummary();
    CHECK( !layerSummary.isValid );
    QJsonObject layerJson = layerSummary.toJsonObject();
    CHECK( layerJson[QStringLiteral( "status" )].toString() == QStringLiteral( "no_active_layer" ) );

    auto canvasSummary = bridge.getCanvasViewportSummary();
    CHECK( !canvasSummary.isValid );
    QJsonObject canvasJson = canvasSummary.toJsonObject();
    CHECK( canvasJson[QStringLiteral( "status" )].toString() == QStringLiteral( "no_canvas" ) );

    CHECK( bridge.activeLayer() == nullptr );
    CHECK_FALSE( bridge.openPath( QStringLiteral( "" ) ) );
    CHECK_FALSE( bridge.pushMessageBarAlert( QStringLiteral( "Title" ), QStringLiteral( "Message" ) ) );
    CHECK_FALSE( bridge.pushMessageBarAlert( QStringLiteral( "Title" ), QStringLiteral( "Message" ),
                                             static_cast<int>( Qgis::MessageLevel::Warning ) ) );
  }

  SECTION( "ActiveViewHost integration maps queries correctly" )
  {
    QgsMapCanvas canvas;
    ActiveViewHost host( &canvas, nullptr, nullptr, nullptr, nullptr,
                         sicnu::display::DisplayViewId(), nullptr );
    bridge.setActiveViewHost( &host );
    CHECK( bridge.activeViewHost() == &host );

    auto canvasSummary = bridge.getCanvasViewportSummary();
    CHECK( canvasSummary.isValid );
    QJsonObject canvasJson = canvasSummary.toJsonObject();
    CHECK( canvasJson[QStringLiteral( "status" )].toString() == QStringLiteral( "ok" ) );
    CHECK( canvasJson.contains( QStringLiteral( "extent" ) ) );
    CHECK( canvasJson.contains( QStringLiteral( "scale" ) ) );
  }

  SECTION( "Host without canvas yields no_canvas status" )
  {
    ActiveViewHost host( nullptr, nullptr, nullptr, nullptr, nullptr,
                         sicnu::display::DisplayViewId(), nullptr );
    bridge.setActiveViewHost( &host );
    auto canvasSummary = bridge.getCanvasViewportSummary();
    CHECK( !canvasSummary.isValid );
    CHECK( canvasSummary.toJsonObject()[QStringLiteral( "status" )].toString()
           == QStringLiteral( "no_canvas" ) );
  }
}




