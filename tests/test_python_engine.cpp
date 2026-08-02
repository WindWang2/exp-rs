#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "python/qgis_python.h"
#include "python/sicnu_python_api.h"
#include "python/sicnu_python_runner.h"
#include "qgspythonrunner.h"

#include "active_view_host.h"
#include "project_context.h"
#include "data/data_manager.h"
#include "display/qgis_display_manager.h"

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgslayertreeview.h>

#include <QFileInfo>
#include <QDir>

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

TEST_CASE( "QgisPython singleton initializes and runs embedded code", "[python][engine]" )
{
  REQUIRE( QgisPython::instance().initialize() );
  CHECK( QgisPython::instance().isInitialized() );

  QString error;
  bool ok = QgisPython::instance().runString( QStringLiteral( "x = 100 + 200" ), error );
  REQUIRE( ok );

  QString result;
  ok = QgisPython::instance().evalString( QStringLiteral( "str(x)" ), result, error );
  REQUIRE( ok );
  CHECK( result == QStringLiteral( "300" ) );
}

TEST_CASE( "QgisPython version and package probes use expression-form eval", "[python][engine]" )
{
  REQUIRE( QgisPython::instance().initialize() );

  // Must be a real version string (not empty / not leftover SyntaxError spam path).
  const QString version = QgisPython::instance().pythonVersion();
  REQUIRE_FALSE( version.isEmpty() );
  REQUIRE( version != QStringLiteral( "Not initialized" ) );
  // Typical sys.version starts with major.minor (e.g. "3.12.0 ...").
  CHECK( version.at( 0 ).isDigit() );

  // Built-in module should be available; nonsense name should not.
  CHECK( QgisPython::instance().isPackageAvailable( QStringLiteral( "sys" ) ) );
  CHECK_FALSE( QgisPython::instance().isPackageAvailable(
    QStringLiteral( "definitely_not_a_real_package_xyz_103" ) ) );
}

TEST_CASE( "QgisPython initialize executes init snippets (sicnu module, stdout redirection, qgis.utils stub)", "[python][engine][init_snippets]" )
{
  REQUIRE( QgisPython::instance().initialize() );

  QString result, error;

  // 1. Verify sicnu helper module exists in sys.modules
  bool ok = QgisPython::instance().evalString( QStringLiteral( "'sicnu' in sys.modules" ), result, error );
  REQUIRE( ok );
  CHECK( result == QStringLiteral( "True" ) );

  // 2. Verify stdout redirection to SICNUStdout
  ok = QgisPython::instance().evalString( QStringLiteral( "type(sys.stdout).__name__" ), result, error );
  REQUIRE( ok );
  CHECK( result == QStringLiteral( "SICNUStdout" ) );

  // 3. Verify qgis.utils stub exists in sys.modules
  ok = QgisPython::instance().evalString( QStringLiteral( "'qgis.utils' in sys.modules" ), result, error );
  REQUIRE( ok );
  CHECK( result == QStringLiteral( "True" ) );
}


TEST_CASE( "qgis.utils stub keeps project clear free of spurious NameError", "[python][engine]" )
{
  REQUIRE( QgisPython::instance().initialize() );
  REQUIRE( QgsPythonRunner::isValid() );

  // The vendored QgsProject::clear() path calls this via QgsPythonRunner;
  // without the stub it raises NameError: name 'qgis' is not defined (#103).
  CHECK( QgsPythonRunner::run( QStringLiteral( "qgis.utils.clean_project_expression_functions()" ) ) );

  // Exercise the real call path.
  QgsProject::instance()->clear();
}

TEST_CASE( "SicnuPythonRunner is registered globally with QgsPythonRunner", "[python][runner]" )
{
  REQUIRE( QgisPython::instance().initialize() );
  REQUIRE( QgsPythonRunner::isValid() );

  QString result;
  bool ok = QgsPythonRunner::eval( QStringLiteral( "5 * 9" ), result );
  REQUIRE( ok );
  CHECK( result == QStringLiteral( "45" ) );

  ok = QgsPythonRunner::run( QStringLiteral( "y = 'sicnu_geo'" ) );
  REQUIRE( ok );

  ok = QgsPythonRunner::eval( QStringLiteral( "y" ), result );
  REQUIRE( ok );
  CHECK( result == QStringLiteral( "sicnu_geo" ) );
}

TEST_CASE( "SicnuPythonApi routes layer addition through ActiveViewHost and Data/Display seam", "[python][api]" )
{
  REQUIRE( QgisPython::instance().initialize() );

  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  ActiveViewHost activeViewHost(&canvas, &treeView, nullptr,
                                &context->dataManager(), &context->displayManager(),
                                context->mainViewId(), nullptr);
  activeViewHost.initLayerTree();

  // ADR 0043: no direct canvas — routes through ActiveViewHost.
  SicnuPythonApi::instance().setActiveViewHost(&activeViewHost);

  const QString demPath = fixturePath(QStringLiteral("samples/dem_sample.tif"));
  const QString addedName = SicnuPythonApi::instance().addRasterLayer(demPath);
  REQUIRE_FALSE(addedName.isEmpty());

  // Verify Data Asset was registered in DataManager
  CHECK(context->dataManager().assets().size() == 1);
  const auto assetId = context->dataManager().assets().first().id();
  CHECK(context->dataManager().leaseCount(assetId) == 1);

  // Verify Display Layer was created in DisplayManager's main view
  const auto mainView = context->displayManager().view(context->mainViewId());
  REQUIRE(mainView);
  CHECK(mainView->layerIds().size() == 1);
  CHECK(project->count() == 1);
}
