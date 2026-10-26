// tests/test_r22_agent_ghost_algo.cpp — R22-04 (#1691) regression.
//
// RED first: PythonPluginAdapter::initialize()'s timeout/failure path
// disconnected the worker and retired the in-flight load RPC but never
// revoked the py: algorithms the plugin had already registered through the
// bound bridge (the success-path unload does, #755). A plugin that
// registers and then stalls past the 5 s load timeout left a ghost catalog
// entry whose executor can only ever throw "IPC Server not available".
// The failure path must revoke exactly like unload does; the AppInterfaceBridge
// destructor is the last-resort net for any other discard path.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "python/isolated/python_plugin_adapter.h"
#include "python/isolated/python_plugin_host.h"

#include "processing/framework/algorithm_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

using sicnu::python::isolated::PythonPluginHost;

int main( int argc, char *argv[] )
{
  QCoreApplication app( argc, argv );
  return Catch::Session().run( argc, argv );
}

namespace
{
/// Writes a plugin whose classFactory registers py:ghost_slow_register and
/// then sleeps past the adapter's 5 s load timeout.
QString makeStallingPlugin( const QTemporaryDir &dir )
{
  const QString pluginDir = dir.path() + QStringLiteral( "/stall_plugin" );
  QDir().mkpath( pluginDir );
  QFile meta( pluginDir + QStringLiteral( "/metadata.txt" ) );
  if ( !meta.open( QIODevice::WriteOnly | QIODevice::Text ) )
    return QString();
  QTextStream( &meta ) << "name=Stall Plugin\nversion=1.0\n";
  meta.close();

  QFile init( pluginDir + QStringLiteral( "/__init__.py" ) );
  if ( !init.open( QIODevice::WriteOnly | QIODevice::Text ) )
    return QString();
  QTextStream( &init ) << QStringLiteral( R"PY(
import time

def classFactory(iface):
    iface.registerProcessingAlgorithm(
        "py:ghost_slow_register",
        "Ghost Slow Register",
        execute_fn=lambda p: {"echo": p},
    )
    # Exceed the adapter's 5 s load timeout AFTER registering.
    time.sleep(8)
    class StallPlugin:
        def __init__(self, iface):
            self.iface = iface
        def initGui(self):
            pass
        def unload(self):
            pass
    return StallPlugin(iface)
)PY" );
  init.close();
  return pluginDir;
}

bool ghostRegistered()
{
  return sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
             QStringLiteral( "py:ghost_slow_register" ).toStdString() )
         != nullptr;
}
} // namespace

TEST_CASE( "a timed-out plugin load leaves no ghost py: algorithm (#1691)",
           "[python][host][r22]" )
{
  // A previous run (or a python_plugins provider path) may leave state;
  // scrub so the verdict is this test's own.
  sicnu::processing::AtomicAlgorithmRegistry::instance().unregisterAdapter(
      QStringLiteral( "py:ghost_slow_register" ).toStdString() );
  REQUIRE_FALSE( ghostRegistered() );

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString pluginDir = makeStallingPlugin( dir );
  REQUIRE_FALSE( pluginDir.isEmpty() );

  // Production parity: the app initializes AlgorithmEngine at startup, which
  // registers the python_plugins provider the bridge prefers. Without it the
  // bridge falls back to the bare registry and the ghost registers there.
  sicnu::AlgorithmEngine::instance().initialize();

  PythonPluginHost host( 2 );
  QString error;
  // The load times out (~5 s) because classFactory sleeps 8 s after
  // registering. A null DataManager still binds the bridge (the headless
  // asset seam keeps it optional), which is what lets the plugin register
  // in the first place.
  PythonPluginAdapter *adapter = host.loadPlugin( pluginDir, nullptr, nullptr, nullptr, &error );
  INFO( "load error: " << error.toStdString() );
  REQUIRE( adapter == nullptr );
  CHECK( error.contains( QStringLiteral( "initialization failed" ) ) );

  // GREEN: the registration made before the timeout was revoked with the
  // failed load. RED (master): the ghost stays in the catalog.
  CHECK_FALSE( ghostRegistered() );

  host.unloadAll();
  CHECK_FALSE( ghostRegistered() );
}
