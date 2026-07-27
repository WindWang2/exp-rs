#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "python/qgis_python.h"
#include "python/sicnu_python_api.h"
#include "python/sicnu_python_runner.h"
#include "python/sicnu_app_interface.h"
#include "python/python_plugin_adapter.h"
#include "plugin_manager.h"

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
