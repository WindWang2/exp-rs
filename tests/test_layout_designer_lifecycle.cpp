// test_layout_designer_lifecycle.cpp — B4: the designer must not outlive its layout
//
// The layout is owned by the project's layout manager; the designer dialog
// is not. Any project clear (New/Open Project), layout removal or direct
// destruction used to leave an open designer rendering a dead scene. These
// cases demand the designer retire itself the moment its layout dies, and
// stay safe (no-ops, no dangling access) afterwards.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QPointer>
#include <QtTest>

#include "qgsmapcanvas.h"
#include "qgsprintlayout.h"
#include "qgslayoutmanager.h"
#include "qgsproject.h"

#include "layout/qgslayoutdesignerdialog.h"

// QGIS thread-local QgsProjContext crashes during glibc atexit cleanup when
// run after a Catch2 process that exercised QgsMapCanvas; bypass it with
// std::_Exit once Catch has reported the final result.
namespace
{
  class FastExitListener : public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;
      void testRunEnded( const Catch::TestRunStats &stats ) override
      {
        // The console reporter may not have printed its summary yet when the
        // listener chain runs — emit the verdict ourselves so the transcript
        // survives std::_Exit.
        const bool ok = !stats.aborting && stats.totals.testCases.failed == 0;
        std::fprintf( stderr, "\n%s: %u/%u assertions, %u/%u test cases\n",
                      ok ? "ALL TESTS PASSED" : "TESTS FAILED",
                      static_cast<unsigned>( stats.totals.assertions.passed ),
                      static_cast<unsigned>( stats.totals.assertions.passed
                                             + stats.totals.assertions.failed ),
                      static_cast<unsigned>( stats.totals.testCases.passed ),
                      static_cast<unsigned>( stats.totals.testCases.passed
                                             + stats.totals.testCases.failed ) );
        std::fflush( stderr );
        std::_Exit( ok ? 0 : 1 );
      }
  };
}
CATCH_REGISTER_LISTENER( FastExitListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_layout_designer_lifecycle";
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

  /// Mirrors the shell's newLayout(): a manager-owned print layout plus a
  /// live designer over it. `deleteOnClose=false` keeps the dialog object
  /// alive after the auto-close so the retired state itself is observable.
  QgsLayoutDesignerDialog *openDesignerOver( QgsPrintLayout *layout, QgsMapCanvas &canvas, bool deleteOnClose )
  {
    auto *designer = new QgsLayoutDesignerDialog( layout, &canvas, nullptr );
    if ( deleteOnClose )
      designer->window()->setAttribute( Qt::WA_DeleteOnClose );
    designer->window()->show();
    QTest::qWait( 20 );
    return designer;
  }
}

TEST_CASE( "Layout designer: closes when the layout manager removes its layout", "[app][layout_designer]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  auto *project = QgsProject::instance();

  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  REQUIRE( project->layoutManager()->addLayout( layout ) );

  // deleteOnClose=false: the dialog object survives its own auto-close, so
  // the retired state is observable through it.
  QgsLayoutDesignerDialog *designer = openDesignerOver( layout, canvas, false );
  REQUIRE( designer->window()->isVisible() );
  REQUIRE( designer->layout() == layout );

  project->layoutManager()->removeLayout( layout );
  QTest::qWait( 40 ); // let the close cascade settle

  // The designer retired with the layout — it must not linger as an open
  // window over a dead scene.
  CHECK( designer->layout() == nullptr );
  CHECK( designer->masterLayout() == nullptr );
  CHECK( !designer->window()->isVisible() );

  // Post-mortem calls are safe no-ops, never dangling access.
  designer->selectItems( {} );

  // A late close() (user or teardown) is idempotent.
  designer->close();
  QTest::qWait( 20 );
  CHECK( !designer->window()->isVisible() );
  delete designer;
}

TEST_CASE( "Layout designer: closes when the project clears all layouts", "[app][layout_designer]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  auto *project = QgsProject::instance();

  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  REQUIRE( project->layoutManager()->addLayout( layout ) );

  QgsLayoutDesignerDialog *designer = openDesignerOver( layout, canvas, false );
  REQUIRE( designer->window()->isVisible() );

  // The New/Open Project boundary: QgsProject::clear() empties the layout
  // manager and deletes every layout under the open designer.
  project->clear();
  QTest::qWait( 40 );

  CHECK( designer->layout() == nullptr );
  CHECK( !designer->window()->isVisible() );
  delete designer;
}

TEST_CASE( "Layout designer: closes when an unmanaged layout is destroyed", "[app][layout_designer]" )
{
  ensureApp();
  QgsMapCanvas canvas;

  // A layout nobody manages (the addLayout-refusal edge): destruction must
  // still retire the designer that was opened over it.
  auto *layout = new QgsPrintLayout( QgsProject::instance() );
  layout->initializeDefaults();

  QgsLayoutDesignerDialog *designer = openDesignerOver( layout, canvas, false );
  REQUIRE( designer->window()->isVisible() );

  delete layout;
  QTest::qWait( 40 );

  CHECK( designer->layout() == nullptr );
  CHECK( !designer->window()->isVisible() );
  delete designer;
}

TEST_CASE( "Layout designer: production WA_DeleteOnClose cascade retires the dialog", "[app][layout_designer]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  auto *project = QgsProject::instance();

  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  REQUIRE( project->layoutManager()->addLayout( layout ) );

  // Exactly as newLayout() opens it: WA_DeleteOnClose on the designer
  // window. Layout death → window close → (pre-existing chain) window
  // destroyed → dialog deleteLater.
  QPointer<QgsLayoutDesignerDialog> designer( openDesignerOver( layout, canvas, true ) );
  REQUIRE( !designer.isNull() );
  REQUIRE( designer->window()->isVisible() );

  project->layoutManager()->removeLayout( layout );
  QTest::qWait( 60 );

  // The whole surface is gone — nothing left to dangle.
  CHECK( designer.isNull() );
}
