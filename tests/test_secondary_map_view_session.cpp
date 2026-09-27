// test_secondary_map_view_session.cpp — B2: secondary view close→reopen lifecycle
//
// The session owns the widget/engine-view/sync state machine. The killer
// contract: the pixel-sync controller is re-created on EVERY open — close()
// deletes it while the widget survives for reuse, so a reopen that only
// rebuilt the widget branch left the dual view permanently unsynced (the
// #1269-remainder B2 gap). Reopen must also land on a fresh engine view and
// never duplicate widget connections.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QAction>
#include <QApplication>
#include <QSplitter>
#include <QtTest>

#include "qgsmapcanvas.h"
#include "qgsproject.h"
#include "qgsrectangle.h"

#include "project_context.h"
#include "shell/rs_dual_viewport_sync_controller.h"
#include "shell/secondary_map_view_session.h"

#include <memory>
#include <algorithm>
#include "support/qt_lifecycle.h"

using Catch::Approx;

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_secondary_map_view_session";
  char *fake_argv[] = { fake_argv0, nullptr };

  /// The lifecycle-relevant link contract: a linked secondary view carries
  /// the primary's extent CENTER. (Canvas-side aspect re-adjustment preserves
  /// centers; exact extent/scale equality on equal-size viewports is the
  /// controller suite's contract — test_dual_viewport_sync.)
  void checkLinkedToPrimary( const QgsMapCanvas &primary, const QgsMapCanvas &secondary )
  {
    const QgsPointXY pc = primary.extent().center();
    const QgsPointXY sc = secondary.extent().center();
    CHECK( sc.x() == Approx( pc.x() ).margin( 1e-3 ) );
    CHECK( sc.y() == Approx( pc.y() ).margin( 1e-3 ) );
  }

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }

  struct Fixture
  {
    QgsMapCanvas mainCanvas;
    QSplitter splitter;
    std::unique_ptr<sicnu::app::ProjectContext> context;
    std::unique_ptr<SecondaryMapSession> session;
    QAction viewAction;
    QAction syncAction;
    sicnu::display::DisplayViewId registered;

    Fixture()
      : viewAction( QStringLiteral( "view" ), &splitter )
      , syncAction( QStringLiteral( "sync" ), &splitter )
    {
      // Mirror the shell: the View toggles are checkable actions…
      viewAction.setCheckable( true );
      syncAction.setCheckable( true );
      // …and the splitter is SHOWN (a child widget is never visible while
      // its parent chain is hidden). Both canvases end up ~300x300 so the
      // aspect-ratio-preserving setExtent lands identically on each.
      mainCanvas.resize( 300, 300 );
      splitter.resize( 600, 300 );
      splitter.show();
      QTest::qWait( 30 );
      // Headless context: the session only creates/removes SECONDARY views,
      // so no main display view is needed (ADR 0023 seam).
      auto created = sicnu::app::ProjectContext::createHeadless();
      if ( !created )
        FAIL( "ProjectContext::createHeadless failed" );
      context = created.take();
      session = std::make_unique<SecondaryMapSession>(
          &mainCanvas, &splitter, context.get(),
          [this]( sicnu::display::DisplayViewId id ) { registered = id; } );
      session->setActions( &viewAction, &syncAction );
    }
  };
}

TEST_CASE( "Secondary view session: open wires widget, engine view and sync", "[app][secondary_view]" )
{
  ensureApp();
  Fixture f;

  REQUIRE( !f.session->isOpen() );
  REQUIRE( f.session->syncController() == nullptr );

  QString error;
  REQUIRE( f.session->open( &error ) );
  REQUIRE( error.isEmpty() );
  QTest::qWait( 50 ); // layout settle for the freshly added canvas

  CHECK( f.session->isOpen() );
  CHECK( f.session->widget() != nullptr );
  CHECK( f.session->widget()->isVisible() );
  CHECK( f.session->widget()->viewId() == f.session->viewId() );
  CHECK( f.registered == f.session->viewId() ); // joined the link authorities
  // Context owns the engine view now.
  CHECK( f.context->views().contains( f.session->viewId() ) );

  // Both View toggles follow the live session.
  CHECK( f.viewAction.isChecked() );
  CHECK( f.syncAction.isChecked() );
  CHECK( f.session->syncController() != nullptr );
  CHECK( f.session->syncController()->isEnabled() );
}

TEST_CASE( "Secondary view session: pixel sync follows the primary viewport", "[app][secondary_view]" )
{
  ensureApp();
  Fixture f;
  REQUIRE( f.session->open() );

  f.mainCanvas.setExtent( QgsRectangle( 10, 10, 30, 30 ) );
  QTest::qWait( 80 ); // 16ms throttle + apply

  checkLinkedToPrimary( f.mainCanvas, *f.session->widget()->canvas() );
  CHECK( f.session->syncController()->stats().appliedSyncCount >= 1 );
}

TEST_CASE( "Secondary view session: close tears sync down but reuses the widget", "[app][secondary_view]" )
{
  ensureApp();
  Fixture f;
  REQUIRE( f.session->open() );
  auto *widgetBefore = f.session->widget();
  const auto viewBefore = f.session->viewId();

  f.session->close();

  CHECK( !f.session->isOpen() );
  CHECK( f.session->viewId().isNull() );
  // THE B2 mechanism: the sync controller is GONE after close…
  CHECK( f.session->syncController() == nullptr );
  // …while the widget survives hidden, ready for reuse.
  CHECK( f.session->widget() == widgetBefore );
  CHECK( !f.session->widget()->isVisible() );
  CHECK( f.session->widget()->viewId().isNull() );
  // Toggles reflect the closed state.
  CHECK( !f.viewAction.isChecked() );
  CHECK( !f.syncAction.isChecked() );
  // The engine view was released.
  CHECK( !f.context->views().contains( viewBefore ) );

  // Closing again is a no-op.
  f.session->close();
  CHECK( !f.session->isOpen() );
}

TEST_CASE( "Secondary view session: reopen re-creates sync and a fresh engine view (B2 kill)", "[app][secondary_view]" )
{
  ensureApp();
  Fixture f;
  REQUIRE( f.session->open() );
  auto *widgetBefore = f.session->widget();
  const auto viewBefore = f.session->viewId();
  f.session->close();

  // While closed, the primary moved on. A reopen must snap the secondary
  // back onto the primary's CURRENT viewport and keep following it.
  f.mainCanvas.setExtent( QgsRectangle( 100, 100, 200, 200 ) );
  QTest::qWait( 30 );

  QString error;
  REQUIRE( f.session->open( &error ) );
  REQUIRE( error.isEmpty() );
  QTest::qWait( 100 ); // settle the fresh canvas (snap + aspect adjustments)

  // THE KILL: sync exists again after reopen. (The pre-fix behavior
  // created the controller only inside the widget-creation branch, so a
  // reopen left syncController() null forever and the View toggle dead.)
  REQUIRE( f.session->syncController() != nullptr );
  CHECK( f.session->isOpen() );
  CHECK( f.session->viewId() != viewBefore ); // fresh engine view
  CHECK( f.context->views().contains( f.session->viewId() ) );
  CHECK( !f.context->views().contains( viewBefore ) );
  CHECK( f.session->widget() == widgetBefore ); // reused, not duplicated
  CHECK( f.viewAction.isChecked() );
  CHECK( f.syncAction.isChecked() );

  // Snapped onto the primary's current viewport…
  checkLinkedToPrimary( f.mainCanvas, *f.session->widget()->canvas() );

  // …and STILL FOLLOWING: a later primary move propagates to the secondary.
  const auto centerBefore = f.session->widget()->canvas()->extent().center();
  f.mainCanvas.setExtent( QgsRectangle( 0, 0, 40, 40 ) );
  QTest::qWait( 120 );
  CHECK( f.session->widget()->canvas()->extent().center().x()
         == Approx( 20.0 ).margin( 1e-3 ) );
  CHECK( f.session->widget()->canvas()->extent().center().x()
         != Approx( centerBefore.x() ).margin( 1e-3 ) );
  // Sync traffic flowed through the re-created controller.
  CHECK( f.session->syncController()->stats().appliedSyncCount >= 1 );
}

TEST_CASE( "Secondary view session: survives a project clear and reopens cleanly", "[app][secondary_view]" )
{
  ensureApp();
  Fixture f;
  REQUIRE( f.session->open() );
  const auto viewBefore = f.session->viewId();

  // Project switch: clearProject tears display layers across ALL views but
  // keeps view records — the open session stays consistent…
  REQUIRE( f.context->clearProject( *QgsProject::instance() ) );
  CHECK( f.session->isOpen() );
  CHECK( f.session->viewId() == viewBefore );
  CHECK( f.session->syncController() != nullptr );

  // …and the boundary afterwards works on the fresh project state.
  f.session->close();
  REQUIRE( f.session->open() );
  CHECK( f.session->viewId() != viewBefore );
  CHECK( f.session->syncController() != nullptr );
}

TEST_CASE( "Secondary view session: destroying the session releases the engine view", "[app][secondary_view]" )
{
  ensureApp();
  Fixture f;
  REQUIRE( f.session->open() );
  const auto view = f.session->viewId();
  REQUIRE( f.context->views().contains( view ) );

  f.session.reset();
  CHECK( !f.context->views().contains( view ) );
}
