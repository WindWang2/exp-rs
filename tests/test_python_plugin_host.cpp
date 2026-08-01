// test_python_plugin_host.cpp — headless Python Plugin Host seam tests (ADR 0023)
#include <catch2/catch_test_macros.hpp>

#include "python_plugin_host.h"
#include "python_plugin_adapter.h"
#include "data/data_manager.h"

#include <QCoreApplication>
#include <QDir>

using namespace sicnu::python::isolated;

TEST_CASE( "PythonPluginHost loads a Python plugin headlessly with a real DataManager", "[python][host]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString error;
  PythonPluginAdapter *adapter = host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &error );
  INFO( error.toStdString() );
  REQUIRE( adapter != nullptr );
  CHECK( !adapter->name().isEmpty() );
  CHECK( host.loadedPlugins() == QStringList{ adapter->name() } );

  host.unloadAll();
  CHECK( host.loadedPlugins().isEmpty() );
}

TEST_CASE( "PythonPluginHost reports a clean error for a missing plugin directory", "[python][host]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  QString error;
  CHECK( host.loadPlugin( QStringLiteral( "/nonexistent/plugin/dir" ), &dataManager, nullptr, nullptr, &error ) == nullptr );
  CHECK( !error.isEmpty() );
}
