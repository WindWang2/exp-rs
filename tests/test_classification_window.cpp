#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QDockWidget>
#include <QFile>

#include "qgsclassificationmainwindow.h"

#include <cstdlib>

// QGIS thread-local QgsProjContext may crash during glibc atexit cleanup
// once qgis_core/qgis_gui has been touched in-process. Mirror the
// FastExitListener pattern from test_georef_window so Catch reports
// results before the destructor sequence runs.
namespace
{
  class FastExitListener : public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;
      void testRunEnded( const Catch::TestRunStats &stats ) override
      {
        std::_Exit( stats.aborting || stats.totals.testCases.failed > 0 ? 1 : 0 );
      }
  };
}
CATCH_REGISTER_LISTENER( FastExitListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      static QApplication app( fake_argc, fake_argv );
      return &app;
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }
}

TEST_CASE( "ClassificationWindow: constructs with 4 docks", "[classify][window]" )
{
  ensureApp();
  QgsClassificationMainWindow w( nullptr );
  REQUIRE( w.findChild<QDockWidget *>( "rsClassListDock" ) != nullptr );
  REQUIRE( w.findChild<QDockWidget *>( "rsClassQuickListDock" ) != nullptr );
  REQUIRE( w.findChild<QDockWidget *>( "rsClassJmDock" ) != nullptr );
  REQUIRE( w.findChild<QDockWidget *>( "rsClassSpectralDock" ) != nullptr );
}

TEST_CASE( "ClassificationWindow: title and central canvas", "[classify][window]" )
{
  ensureApp();
  QgsClassificationMainWindow w( nullptr );
  REQUIRE( w.windowTitle().contains( "Classification" ) );
  REQUIRE( w.centralWidget() != nullptr );
}

TEST_CASE( "ClassificationWindow: apply and preview submit through Task Center", "[classify][task_center]" )
{
  QFile source( QStringLiteral( SICNU_SOURCE_DIR "/src/app/classification/qgsclassificationmainwindow.cpp" ) );
  REQUIRE( source.open( QIODevice::ReadOnly | QIODevice::Text ) );
  const QString text = QString::fromUtf8( source.readAll() );

  const auto methodBody = [&text]( const QString &signature, const QString &nextSignature ) {
    const int begin = text.indexOf( signature );
    REQUIRE( begin >= 0 );
    const int end = text.indexOf( nextSignature, begin + signature.size() );
    REQUIRE( end >= 0 );
    return text.mid( begin, end - begin );
  };

  const QString apply = methodBody(
    QStringLiteral( "void QgsClassificationMainWindow::applyClassification()" ),
    QStringLiteral( "void QgsClassificationMainWindow::applyPreview()" ) );
  const QString preview = methodBody(
    QStringLiteral( "void QgsClassificationMainWindow::applyPreview()" ),
    QStringLiteral( "void QgsClassificationMainWindow::openPostProcessDialog" ) );

  REQUIRE( apply.contains( QStringLiteral( "TaskCenter::instance().submitJob" ) ) );
  REQUIRE_FALSE( apply.contains( QStringLiteral( "RsJobRunner::run" ) ) );
  REQUIRE( preview.contains( QStringLiteral( "TaskCenter::instance().submitJob" ) ) );
  REQUIRE_FALSE( preview.contains( QStringLiteral( "RsJobRunner::run" ) ) );
}
