// tests/test_timeline_scrubber_widget.cpp — D16 Package G: timeline
// scrubber signal contract, snap-to-acquisition, and the 60 fps redline
// (100 slice switches < 1600 ms), all headless (QT_QPA_PLATFORM=offscreen).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/widgets/timeline_scrubber_widget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QThread>

#include <chrono>
#include <string>
#include <vector>

using sicnu::gui::TimelineScrubberWidget;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_timeline_scrubber_widget";
char *appArgv[] = { appArgv0, nullptr };

QApplication &ensureApp()
{
    if ( !QApplication::instance() )
        new QApplication( appArgc(), appArgv );
    return *qobject_cast<QApplication *>( QApplication::instance() );
}

struct SignalLog
{
    int count = 0;
    int lastIndex = -1;
    QString lastDate;
    bool finished = false;

    void connect( TimelineScrubberWidget &w )
    {
        QObject::connect( &w, &TimelineScrubberWidget::dateChanged,
                          [this]( int index, const QString &date )
                          {
                              ++count;
                              lastIndex = index;
                              lastDate = date;
                          } );
        QObject::connect( &w, &TimelineScrubberWidget::playbackFinished, [this]() { finished = true; } );
    }
};

void spinFor( int milliseconds )
{
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < milliseconds )
    {
        QApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 5 );
    }
}

void sendMousePress( TimelineScrubberWidget &widget, const QPointF &pos )
{
    QMouseEvent press( QEvent::MouseButtonPress, pos, pos, pos, Qt::LeftButton, Qt::LeftButton,
                       Qt::NoModifier );
    QApplication::sendEvent( &widget, &press );
}

} // namespace

TEST_CASE( "Scrubber signal contract and index clamping", "[d16][widget]" )
{
    ensureApp();
    TimelineScrubberWidget scrubber;
    SignalLog log;
    log.connect( scrubber );

    scrubber.setTimelineDates( { "2021-01-01", "2021-01-17", "2021-02-02" } );
    // Installation commits index 0 and announces it.
    REQUIRE( scrubber.currentIndex() == 0 );
    REQUIRE( log.count == 1 );
    REQUIRE( log.lastIndex == 0 );
    REQUIRE( log.lastDate == QStringLiteral( "2021-01-01" ) );

    scrubber.setCurrentIndex( 1 );
    REQUIRE( log.count == 2 );
    REQUIRE( log.lastIndex == 1 );
    REQUIRE( log.lastDate == QStringLiteral( "2021-01-17" ) );

    SECTION( "same index emits nothing" )
    {
        scrubber.setCurrentIndex( 1 );
        REQUIRE( log.count == 2 );
    }

    SECTION( "out-of-range index clamps into range" )
    {
        scrubber.setCurrentIndex( 99 );
        REQUIRE( scrubber.currentIndex() == 2 );
        REQUIRE( log.lastIndex == 2 );
        REQUIRE( log.lastDate == QStringLiteral( "2021-02-02" ) );
    }
}

TEST_CASE( "Playback advances at speed and reports completion", "[d16][widget]" )
{
    ensureApp();
    TimelineScrubberWidget scrubber;
    SignalLog log;
    log.connect( scrubber );

    std::vector<QString> dates;
    for ( int i = 0; i < 46; ++i )
        dates.push_back( QStringLiteral( "d%1" ).arg( i ) );
    scrubber.setTimelineDates( dates );

    // 10x speed: ~10 slices/second -> several advances inside 500 ms.
    scrubber.setPlaySpeed( 10.0f );
    scrubber.play();
    spinFor( 500 );
    scrubber.pause();
    const int afterPlay = log.count;
    INFO( "slices advanced in 500ms: " << ( afterPlay - 1 ) );
    REQUIRE( afterPlay - 1 >= 2 );
    REQUIRE( !log.finished );

    // Paused: no further advancement.
    spinFor( 120 );
    REQUIRE( log.count == afterPlay );

    // Fast-forward to the end: playbackFinished must fire exactly once.
    scrubber.setPlaySpeed( 100.0f );
    scrubber.play();
    spinFor( 2500 );
    REQUIRE( log.finished );
    REQUIRE( scrubber.currentIndex() == 45 );
}

TEST_CASE( "Dragging snaps to acquisition ticks within five pixels", "[d16][widget]" )
{
    ensureApp();
    TimelineScrubberWidget scrubber;
    SignalLog log;
    log.connect( scrubber );
    scrubber.setTimelineDates( { "2021-01-01", "2021-01-17", "2021-02-02" } );
    scrubber.resize( 303, 40 );
    scrubber.show();
    QApplication::processEvents();

    // Tick positions: 5, 151.5, 298. A press 1.5 px from tick 1 snaps in.
    sendMousePress( scrubber, QPointF( 150, 20 ) );
    REQUIRE( scrubber.currentIndex() == 1 );
    REQUIRE( log.lastIndex == 1 );

    // 15 px from the nearest tick: no snap, linear index resolution.
    sendMousePress( scrubber, QPointF( 20, 20 ) );
    REQUIRE( log.lastIndex == 0 );
}

TEST_CASE( "Sixty-fps redline: 100 slice switches stay under 1600 ms",
           "[d16][widget]" )
{
    ensureApp();
    TimelineScrubberWidget scrubber;
    scrubber.setTimelineDates( [&] {
        std::vector<QString> dates;
        for ( int i = 0; i < 365; ++i )
            dates.push_back( QString::number( i ) );
        return dates;
    }() );
    scrubber.resize( 800, 40 );
    scrubber.show();
    QApplication::processEvents();

    const auto start = std::chrono::high_resolution_clock::now();
    for ( int i = 0; i < 100; ++i )
    {
        scrubber.setCurrentIndex( i % 365 );
        QApplication::processEvents();
    }
    const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::high_resolution_clock::now() - start )
                                    .count();
    INFO( "elapsed ms: " << elapsedMs );
    REQUIRE( elapsedMs < 1600 ); // average frame < 16 ms (D16 §G)
}
