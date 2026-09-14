/***************************************************************************
  app/widgets/timeline_scrubber_widget.cpp
  Temporal Phenology Timeline Studio (D16) — timeline scrubber.
  ---------------------------
  See timeline_scrubber_widget.h for the seam contract (ADR 0161).
 ***************************************************************************/

#include "app/widgets/timeline_scrubber_widget.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace sicnu::gui
{

TimelineScrubberWidget::TimelineScrubberWidget( QWidget *parent )
  : QWidget( parent )
{
    setMinimumHeight( 40 );
    connect( &mTimer, &QTimer::timeout, this, &TimelineScrubberWidget::onTick );
    mTimer.setInterval( kFrameMs ); // 60 fps pulse (D16 §G)
}

void TimelineScrubberWidget::setTimelineDates( const std::vector<QString> &isoDates )
{
    mDates = isoDates;
    mCurrentIndex = mDates.empty() ? -1 : 0;
    mFrameAccumulator = 0.0f;
    update();
    if ( mCurrentIndex >= 0 )
        emit dateChanged( mCurrentIndex, mDates[static_cast<std::size_t>( mCurrentIndex )] );
}

void TimelineScrubberWidget::setCurrentIndex( int index )
{
    if ( mDates.empty() )
        return;
    const int clamped = std::clamp( index, 0, static_cast<int>( mDates.size() ) - 1 );
    if ( clamped == mCurrentIndex )
        return;
    mCurrentIndex = clamped;
    mFrameAccumulator = 0.0f;
    update();
    emit dateChanged( mCurrentIndex, mDates[static_cast<std::size_t>( mCurrentIndex )] );
}

void TimelineScrubberWidget::play()
{
    if ( mDates.empty() )
        return;
    mTimer.start();
}

void TimelineScrubberWidget::pause()
{
    mTimer.stop();
}

void TimelineScrubberWidget::setPlaySpeed( float speedMultiplier )
{
    mPlaySpeed = std::max( 0.01f, speedMultiplier );
}

void TimelineScrubberWidget::onTick()
{
    if ( mDates.empty() || mCurrentIndex < 0 )
        return;
    mFrameAccumulator += mPlaySpeed;
    while ( mFrameAccumulator >= kFramesPerSlice )
    {
        mFrameAccumulator -= kFramesPerSlice;
        if ( mCurrentIndex + 1 < static_cast<int>( mDates.size() ) )
        {
            setCurrentIndex( mCurrentIndex + 1 );
        }
        else
        {
            pause();
            emit playbackFinished();
            return;
        }
    }
}

int TimelineScrubberWidget::indexAtX( int x ) const
{
    if ( mDates.empty() )
        return -1;
    const int usable = std::max( 1, width() - 2 * kSnapPixels );
    const double fraction = static_cast<double>( std::clamp( x, kSnapPixels, width() - kSnapPixels ) - kSnapPixels ) /
                            usable;
    int index = static_cast<int>( std::lround( fraction * ( mDates.size() - 1 ) ) );
    index = std::clamp( index, 0, static_cast<int>( mDates.size() ) - 1 );
    // Snap-to-acquisition: within kSnapPixels of the nearest tick, commit
    // to that tick exactly.
    const int nearestX = tickX( index );
    if ( std::abs( x - nearestX ) <= kSnapPixels )
        return index;
    const int count = static_cast<int>( mDates.size() );
    const int left = std::clamp( index - 1, 0, count - 1 );
    const int right = std::clamp( index + 1, 0, count - 1 );
    if ( std::abs( x - tickX( left ) ) <= kSnapPixels )
        return left;
    if ( std::abs( x - tickX( right ) ) <= kSnapPixels )
        return right;
    return index;
}

int TimelineScrubberWidget::tickX( int index ) const
{
    if ( mDates.size() < 2 )
        return width() / 2;
    const int usable = std::max( 1, width() - 2 * kSnapPixels );
    return kSnapPixels + static_cast<int>( std::lround(
                             static_cast<double>( index ) / ( mDates.size() - 1 ) * usable ) );
}

void TimelineScrubberWidget::paintEvent( QPaintEvent * )
{
    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.fillRect( rect(), palette().window() );

    const int baseline = height() / 2;
    if ( mDates.empty() )
    {
        painter.setPen( palette().mid().color() );
        painter.drawText( rect(), Qt::AlignCenter, tr( "No timeline" ) );
        return;
    }

    // Rail and acquisition ticks.
    painter.setPen( palette().mid().color() );
    painter.drawLine( 0, baseline, width(), baseline );
    const int count = static_cast<int>( mDates.size() );
    for ( int i = 0; i < count; ++i )
    {
        const int x = tickX( i );
        painter.drawLine( x, baseline - 4, x, baseline + 4 );
    }

    // Current slice marker + date label.
    if ( mCurrentIndex >= 0 && mCurrentIndex < count )
    {
        const int x = tickX( mCurrentIndex );
        painter.setPen( palette().highlight().color() );
        painter.drawLine( x, baseline - 12, x, baseline + 12 );
        painter.setPen( palette().text().color() );
        painter.drawText( rect().adjusted( 0, 0, 0, -baseline ), Qt::AlignHCenter | Qt::AlignTop,
                          mDates[static_cast<std::size_t>( mCurrentIndex )] );
    }
}

void TimelineScrubberWidget::mousePressEvent( QMouseEvent *event )
{
    const int index = indexAtX( static_cast<int>( event->position().x() ) );
    if ( index >= 0 )
        setCurrentIndex( index );
}

void TimelineScrubberWidget::mouseMoveEvent( QMouseEvent *event )
{
    if ( !( event->buttons() & Qt::LeftButton ) )
        return;
    const int index = indexAtX( static_cast<int>( event->position().x() ) );
    if ( index >= 0 )
        setCurrentIndex( index );
}

} // namespace sicnu::gui
