// test_timeline_scrubber_teardown_r4.cpp — WP-C teardown fixtures:
// TimelineScrubberWidget playback-timer lifecycle (Temporal Phenology D16).
//
// Contract under test: a scrubber destroyed while PLAYING must not deliver
// any further 16 ms timer ticks into freed memory, and the process must
// reach a clean exit() afterwards. Truth sources: Qt — a QTimer member is
// stopped and unregistered by ~QObject before the widget memory is freed;
// widget header contract (playbackFinished() at the last slice, state
// machine Paused/Playing/Seeking).
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QPointer>
#include <QtTest>

#include <memory>
#include <vector>

#include "widgets/timeline_scrubber_widget.h"

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_timeline_scrubber_teardown_r4";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
  }

  std::vector<QString> sampleDates()
  {
    return { QStringLiteral( "2025-01-01" ), QStringLiteral( "2025-02-01" ),
             QStringLiteral( "2025-03-01" ), QStringLiteral( "2025-04-01" ) };
  }
} // namespace

TEST_CASE( "Timeline scrubber teardown: destroyed while playing stops ticks", "[teardown][r4]" )
{
  ensureApp();
  int dateChangedCount = 0;
  QPointer<sicnu::gui::TimelineScrubberWidget> guard;
  {
    sicnu::gui::TimelineScrubberWidget w;
    guard = &w;
    w.setTimelineDates( sampleDates() );
    QObject::connect( &w, &sicnu::gui::TimelineScrubberWidget::dateChanged,
                      [&]( int, const QString & ) { ++dateChangedCount; } );
    w.setCurrentIndex( 0 );
    w.play();
    // ~4 ticks at 16 ms advance frames; dateChanged commits require a full
    // slice (~0.5 s), so only assert the widget stayed live and ticking
    // state was entered via play() (index still valid).
    QTest::qWait( 60 );
    REQUIRE( guard.data() == &w );
    REQUIRE( w.currentIndex() >= 0 );
  }
  // The 16 ms pulse must die with the widget: two more pulse periods must
  // not crash and the guard must be null (no post-mortem notifications).
  QTest::qWait( 60 );
  REQUIRE( guard.isNull() );
}

TEST_CASE( "Timeline scrubber teardown: pause-then-delete ends state machine", "[teardown][r4]" )
{
  ensureApp();
  bool finishedFired = false;
  QPointer<sicnu::gui::TimelineScrubberWidget> guard;
  {
    sicnu::gui::TimelineScrubberWidget w;
    guard = &w;
    w.setTimelineDates( sampleDates() );
    QObject::connect( &w, &sicnu::gui::TimelineScrubberWidget::playbackFinished,
                      [&] { finishedFired = true; } );
    w.setCurrentIndex( 0 );
    w.play();
    QTest::qWait( 20 );
    w.pause();
  }
  QTest::qWait( 40 );
  REQUIRE( guard.isNull() );
  REQUIRE_FALSE( finishedFired );
}

TEST_CASE( "Timeline scrubber teardown: playback reaches final slice and finishes", "[teardown][r4]" )
{
  ensureApp();
  std::unique_ptr<sicnu::gui::TimelineScrubberWidget> w =
    std::make_unique<sicnu::gui::TimelineScrubberWidget>();
  w->setTimelineDates( sampleDates() );
  int finishedCount = 0;
  QObject::connect( w.get(), &sicnu::gui::TimelineScrubberWidget::playbackFinished,
                    [&] { ++finishedCount; } );
  w->setCurrentIndex( 0 );
  w->play();
  // 4 slices * 30 frames * 16 ms ~= 1.9 s at 1.0x; run the loop until the
  // documented contract fires or the budget expires (event-loop truth, no
  // implementation peeking).
  const int budgetMs = 4000;
  int waited = 0;
  while ( finishedCount == 0 && waited < budgetMs )
  {
    QTest::qWait( 100 );
    waited += 100;
  }
  REQUIRE( finishedCount >= 1 );
  QPointer<sicnu::gui::TimelineScrubberWidget> guard( w.get() );
  w.reset();
  QTest::qWait( 40 );
  REQUIRE( guard.isNull() );
}
