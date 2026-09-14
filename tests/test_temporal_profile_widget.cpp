// tests/test_temporal_profile_widget.cpp — D16 Package G: temporal profile
// chart (layered rendering, hover contract, cached-background scrub proxy).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/workbench/temporal_timeline_widget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPixmap>
#include <QThread>

#include <cmath>
#include <limits>
#include <vector>

using sicnu::gui::TemporalProfileWidget;
using sicnu::gui::TemporalTimelineWidget;

namespace
{

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_temporal_profile_widget";
char *appArgv[] = { appArgv0, nullptr };

QApplication &ensureApp()
{
    if ( !QApplication::instance() )
        new QApplication( appArgc(), appArgv );
    return *qobject_cast<QApplication *>( QApplication::instance() );
}

} // namespace

TEST_CASE( "Profile renders layered content without crashing on degenerate data",
           "[d16][widget]" )
{
    ensureApp();
    TemporalProfileWidget profile;
    profile.resize( 400, 220 );
    profile.show();
    QApplication::processEvents();

    SECTION( "empty widget renders" )
    {
        const QPixmap frame = profile.grab();
        REQUIRE( !frame.isNull() );
        REQUIRE( frame.size() == profile.size() );
    }

    SECTION( "full data set renders" )
    {
        std::vector<double> t;
        std::vector<float> raw;
        std::vector<float> smooth;
        for ( int i = 0; i < 46; ++i )
        {
            const double day = 16.0 * i;
            t.push_back( day );
            raw.push_back( static_cast<float>( 0.5 + 0.3 * std::sin( 2.0 * M_PI * day / 365.25 ) +
                                              0.02 * std::sin( 7.0 * i ) ) );
            smooth.push_back( static_cast<float>( 0.5 + 0.3 * std::sin( 2.0 * M_PI * day / 365.25 ) ) );
        }
        profile.setRawObservations( t, raw );
        profile.setSmoothedCurve( t, smooth );
        profile.setPhenologyInterval( 60.0, 182.0, 300.0 );
        profile.setBreakpoints( { 365.0 } );

        const QPixmap frame = profile.grab();
        REQUIRE( !frame.isNull() );
        REQUIRE( frame.size() == profile.size() );
    }

    SECTION( "NaN-heavy and reversed phenology input do not crash" )
    {
        std::vector<double> t = { 0, 16, 32, 48 };
        std::vector<float> raw = { kNan, kNan, kNan, kNan };
        profile.setRawObservations( t, raw );
        profile.setPhenologyInterval( 300.0, 200.0, 100.0 ); // reversed: hidden
        profile.setBreakpoints( { std::numeric_limits<double>::quiet_NaN() } );
        REQUIRE( !profile.grab().isNull() );
    }
}

TEST_CASE( "Hovering near a raw observation emits its exact coordinates",
           "[d16][widget]" )
{
    ensureApp();
    TemporalProfileWidget profile;
    profile.resize( 500, 240 );
    profile.show();
    QApplication::processEvents();

    std::vector<double> t;
    std::vector<float> raw;
    for ( int i = 0; i < 23; ++i )
    {
        t.push_back( 16.0 * i );
        raw.push_back( 0.5f );
    }
    profile.setRawObservations( t, raw );

    double hoveredDays = -1.0;
    float hoveredValue = 0.0f;
    int hoverCount = 0;
    QObject::connect( &profile, &TemporalProfileWidget::sampleHovered,
                      [&]( double days, float value )
                      {
                          hoveredDays = days;
                          hoveredValue = value;
                          ++hoverCount;
                      } );

    // Sweep the cursor along the vertical center: every sample sits there.
    for ( int x = 40; x < 490; x += 4 )
    {
        QMouseEvent move( QEvent::MouseMove, QPointF( x, 120 ), QPointF( x, 120 ), QPointF( x, 120 ),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier );
        QApplication::sendEvent( &profile, &move );
    }
    QApplication::processEvents();
    INFO( "hover count: " << hoverCount );
    REQUIRE( hoverCount > 0 );

    // The reported coordinates must be one of the raw samples exactly.
    bool matched = false;
    for ( std::size_t i = 0; i < t.size(); ++i )
        if ( t[i] == hoveredDays && raw[i] == hoveredValue )
            matched = true;
    REQUIRE( matched );
}

TEST_CASE( "Hover-only scrubbing keeps the render fast (double-buffer proxy)",
           "[d16][widget]" )
{
    ensureApp();
    TemporalProfileWidget profile;
    profile.resize( 500, 240 );
    profile.show();
    QApplication::processEvents();

    std::vector<double> t;
    std::vector<float> raw;
    for ( int i = 0; i < 365; ++i )
    {
        t.push_back( static_cast<double>( i ) );
        raw.push_back( static_cast<float>( 0.5 + 0.3 * std::sin( 2.0 * M_PI * i / 365.0 ) ) );
    }
    profile.setRawObservations( t, raw );
    profile.grab(); // build the background cache once

    const auto start = std::chrono::high_resolution_clock::now();
    for ( int i = 0; i < 100; ++i )
    {
        profile.setScrubberHoverDate( static_cast<double>( i ) );
        QApplication::processEvents();
    }
    const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::high_resolution_clock::now() - start )
                                    .count();
    INFO( "hover ms: " << elapsedMs );
    REQUIRE( elapsedMs < 200 );
}
