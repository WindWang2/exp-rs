#pragma once
// support/qt_lifecycle.h — shared ordered-teardown for Qt/QGIS test binaries.
//
// Replaces the per-file FastExitListener(std::_Exit) workaround (cluster A,
// PR #1336 "未解决项" 1) with the production teardown sequence: the same
// ordering src/app/main.cpp:626-636 uses at GUI exit, and the cache
// invalidation contract documented at src/core/qgsapplication.cpp:647-657
// ("invalidate coordinate cache while the PROJ context held by the
// thread-local QgsProjContextStore object is still alive").
//
// Usage contract (retirement pattern, see .planning/qt-teardown-lifecycle-r4/):
//   1. Register the listener:  CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )
//   2. Own the app on the heap, NOT as a function-local static value:
//        static QApplication *app = new QApplication( argc, argv ); return app;
//      A value static registers its destructor with __cxa_atexit, which runs
//      it inside glibc exit() — after every QGIS global cache/registry static
//      created during the run, i.e. exactly the destruction-order hazard that
//      the old std::_Exit defenses masked. Heap ownership has no atexit
//      registration; the listener below deletes it in controlled order.
//   3. Do not call std::_Exit anywhere. Returning normally from main (and
//      surviving glibc exit()) is the retirement assertion itself.
//
// The listener prints the same "ALL TESTS PASSED" stderr marker the retired
// ok-variant listeners printed (docs/development/hardening/
// workbench-project-lifecycle-shell-fixtures/02-test-ledger.md format).

#include <QCoreApplication>
#include <QEvent>

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdio>

#include <qgsapplication.h>

namespace sicnu::test::qtlifecycle
{
  // Heap-owned application objects; deleted by orderlyTeardown(), never by
  // process exit. Call once per binary, from the first test's app getter.
  inline QApplication *heapQApplication( int &argc, char **argv )
  {
    if ( QCoreApplication::instance() )
      return qobject_cast<QApplication *>( QCoreApplication::instance() );
    return new QApplication( argc, argv );
  }

  // Core-only variant for widget-less binaries (same teardown contract).
  inline QCoreApplication *heapQCoreApplication( int &argc, char **argv )
  {
    if ( QCoreApplication::instance() )
      return QCoreApplication::instance();
    return new QCoreApplication( argc, argv );
  }

  inline QgsApplication *heapQgsApplication( int &argc, char **argv, bool guiEnabled )
  {
    if ( QCoreApplication::instance() )
      return qobject_cast<QgsApplication *>( QCoreApplication::instance() );
    QgsApplication *app = new QgsApplication( argc, argv, guiEnabled );
    return app;
  }

  // Ordered teardown, mirroring main() exit in src/app/main.cpp:
  //   drain deferred deletes → exitQgis() (joins the global thread pool,
  //   deletes registry-owned state, invalidates CRS/transform/ellipsoid
  //   caches while the thread-local PROJ context is still alive) → delete
  //   the application object last. Safe on binaries that never touched QGIS:
  //   every step guards on "don't create just to delete".
  inline void orderlyTeardown()
  {
    if ( QCoreApplication::instance() )
      QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    QgsApplication::exitQgis();
    QCoreApplication *app = QCoreApplication::instance();
    delete app;
  }

  class TeardownListener : public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;

      void testRunEnded( const Catch::TestRunStats &stats ) override
      {
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
        // No std::_Exit: run the ordered teardown and return into Catch's
        // main. The process must survive glibc exit() on its own.
        orderlyTeardown();
      }
  };
} // namespace sicnu::test::qtlifecycle
