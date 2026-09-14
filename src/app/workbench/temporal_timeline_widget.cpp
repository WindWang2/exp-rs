/***************************************************************************
  app/workbench/temporal_timeline_widget.cpp
  Temporal Phenology Timeline Studio (D16) — profile chart + composite.
  ---------------------------
  See temporal_timeline_widget.h for the seam contract (ADR 0161).
 ***************************************************************************/

#include "app/workbench/temporal_timeline_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace sicnu::gui
{

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int kHoverSnapPixels = 20;
} // namespace

// --- TemporalProfileWidget -------------------------------------------------

TemporalProfileWidget::TemporalProfileWidget( QWidget *parent )
  : QWidget( parent )
{
    setMinimumHeight( 160 );
    setMouseTracking( true );
}

QRectF TemporalProfileWidget::plotRect() const
{
    return QRectF( 45, 8, width() - 60, height() - 40 );
}

QPointF TemporalProfileWidget::toPixel( double tDays, double value ) const
{
    double t0 = -kNaN;
    double t1 = kNaN;
    for ( double v : mRawT )
    {
        t0 = std::isfinite( t0 ) ? std::min( t0, v ) : v;
        t1 = std::isfinite( t1 ) ? std::max( t1, v ) : v;
    }
    for ( double v : mSmoothT )
    {
        t0 = std::isfinite( t0 ) ? std::min( t0, v ) : v;
        t1 = std::isfinite( t1 ) ? std::max( t1, v ) : v;
    }
    if ( mHasPhenology && std::isfinite( mSos ) && std::isfinite( mEos ) )
    {
        t0 = std::isfinite( t0 ) ? std::min( t0, mSos ) : mSos;
        t1 = std::isfinite( t1 ) ? std::max( t1, mEos ) : mEos;
    }
    if ( !std::isfinite( t0 ) || !std::isfinite( t1 ) || t1 <= t0 )
    {
        t0 = 0.0;
        t1 = 1.0;
    }
    const QRectF plot = plotRect();
    const double x = plot.left() + ( tDays - t0 ) / ( t1 - t0 ) * plot.width();
    double v0 = 0.0;
    double v1 = 1.0;
    for ( std::size_t i = 0; i < mRawV.size() && i < mRawT.size(); ++i )
    {
        if ( !std::isfinite( mRawV[i] ) )
            continue;
        v0 = std::isfinite( v0 ) ? std::min<double>( v0, mRawV[i] ) : mRawV[i];
        v1 = std::isfinite( v1 ) ? std::max<double>( v1, mRawV[i] ) : mRawV[i];
    }
    for ( std::size_t i = 0; i < mSmoothV.size() && i < mSmoothT.size(); ++i )
    {
        if ( !std::isfinite( mSmoothV[i] ) )
            continue;
        v0 = std::isfinite( v0 ) ? std::min<double>( v0, mSmoothV[i] ) : mSmoothV[i];
        v1 = std::isfinite( v1 ) ? std::max<double>( v1, mSmoothV[i] ) : mSmoothV[i];
    }
    if ( !( v1 > v0 ) )
    {
        v0 = 0.0;
        v1 = 1.0;
    }
    const double pad = 0.1 * ( v1 - v0 );
    const double y = plot.bottom() - ( value - ( v0 - pad ) ) / ( ( v1 + pad ) - ( v0 - pad ) ) * plot.height();
    return QPointF( x, y );
}

void TemporalProfileWidget::rebuildBackground()
{
    mBackground = QPixmap( size() );
    mBackground.fill( palette().window().color() );
    QPainter painter( &mBackground );
    painter.setRenderHint( QPainter::Antialiasing );

    const QRectF plot = plotRect();
    painter.setPen( palette().mid().color() );
    painter.drawRect( plot );
    for ( int i = 1; i < 4; ++i )
    {
        const double y = plot.top() + plot.height() * i / 4.0;
        painter.drawLine( QPointF( plot.left(), y ), QPointF( plot.right(), y ) );
    }

    // Phenology season band (SOS..EOS) with the POS marker.
    if ( mHasPhenology && std::isfinite( mSos ) && std::isfinite( mEos ) && mSos < mEos )
    {
        const QPointF leftBottom = toPixel( mSos, 0.0 );
        const QPointF rightBottom = toPixel( mEos, 0.0 );
        QRectF band( leftBottom.x(), plot.top(), rightBottom.x() - leftBottom.x(), plot.height() );
        QColor bandColor = palette().highlight().color();
        bandColor.setAlpha( 45 );
        painter.fillRect( band, bandColor );
        const QPointF posPx = toPixel( mPos, 0.0 );
        painter.setPen( palette().highlight().color() );
        painter.drawLine( QPointF( posPx.x(), plot.top() ), QPointF( posPx.x(), plot.bottom() ) );
    }

    // Breakpoint marks (dashed vertical lines).
    if ( !mBreaks.empty() )
    {
        QPen pen( palette().mid().color(), 1, Qt::DashLine );
        painter.setPen( pen );
        for ( double b : mBreaks )
        {
            if ( !std::isfinite( b ) )
                continue;
            const QPointF px = toPixel( b, 0.0 );
            painter.drawLine( QPointF( px.x(), plot.top() ), QPointF( px.x(), plot.bottom() ) );
        }
    }

    // Raw observations (scatter).
    painter.setPen( palette().text().color() );
    for ( std::size_t i = 0; i < mRawT.size() && i < mRawV.size(); ++i )
    {
        if ( !std::isfinite( mRawT[i] ) || !std::isfinite( mRawV[i] ) )
            continue;
        const QPointF px = toPixel( mRawT[i], mRawV[i] );
        painter.drawEllipse( px, 2.0, 2.0 );
    }

    // Smoothed curve (polyline over finite samples).
    if ( !mSmoothT.empty() )
    {
        painter.setPen( palette().highlight().color() );
        QPointF previous;
        bool havePrevious = false;
        for ( std::size_t i = 0; i < mSmoothT.size() && i < mSmoothV.size(); ++i )
        {
            if ( !std::isfinite( mSmoothT[i] ) || !std::isfinite( mSmoothV[i] ) )
            {
                havePrevious = false;
                continue;
            }
            const QPointF px = toPixel( mSmoothT[i], mSmoothV[i] );
            if ( havePrevious )
                painter.drawLine( previous, px );
            previous = px;
            havePrevious = true;
        }
    }

    mBackgroundDirty = false;
}

void TemporalProfileWidget::setRawObservations( const std::vector<double> &tDays,
                                                const std::vector<float> &values )
{
    mRawT = tDays;
    mRawV = values;
    mBackgroundDirty = true;
    update();
}

void TemporalProfileWidget::setSmoothedCurve( const std::vector<double> &tDays,
                                              const std::vector<float> &values )
{
    mSmoothT = tDays;
    mSmoothV = values;
    mBackgroundDirty = true;
    update();
}

void TemporalProfileWidget::setPhenologyInterval( double sos, double pos, double eos )
{
    mSos = sos;
    mPos = pos;
    mEos = eos;
    mHasPhenology = std::isfinite( sos ) && std::isfinite( pos ) && std::isfinite( eos );
    mBackgroundDirty = true;
    update();
}

void TemporalProfileWidget::setBreakpoints( const std::vector<double> &breakDays )
{
    mBreaks = breakDays;
    mBackgroundDirty = true;
    update();
}

void TemporalProfileWidget::setScrubberHoverDate( double tDays )
{
    mHoverDays = tDays;
    mHasHover = std::isfinite( tDays );
    update(); // dynamic layer only — the cached background is untouched
}

void TemporalProfileWidget::paintEvent( QPaintEvent * )
{
    if ( mBackgroundDirty || mBackground.size() != size() )
        rebuildBackground();
    QPainter painter( this );
    painter.drawPixmap( 0, 0, mBackground );

    // Dynamic foreground: scrubber hover cursor.
    if ( mHasHover )
    {
        const QRectF plot = plotRect();
        const QPointF px = toPixel( mHoverDays, 0.0 );
        if ( px.x() >= plot.left() && px.x() <= plot.right() )
        {
            QPen pen( palette().text().color(), 1, Qt::DashLine );
            painter.setPen( pen );
            painter.drawLine( QPointF( px.x(), plot.top() ), QPointF( px.x(), plot.bottom() ) );
        }
    }
}

void TemporalProfileWidget::mouseMoveEvent( QMouseEvent *event )
{
    const QPointF pos = event->position();
    const QRectF plot = plotRect();
    if ( !plot.adjusted( -kHoverSnapPixels, 0, kHoverSnapPixels, 0 ).contains( pos ) )
        return;

    // Nearest raw sample within the snap radius emits the hover signal.
    double bestDays = 0.0;
    float bestValue = 0.0f;
    double bestDistance = kHoverSnapPixels + 1.0;
    for ( std::size_t i = 0; i < mRawT.size() && i < mRawV.size(); ++i )
    {
        if ( !std::isfinite( mRawT[i] ) || !std::isfinite( mRawV[i] ) )
            continue;
        const QPointF px = toPixel( mRawT[i], mRawV[i] );
        const double distance = std::hypot( px.x() - pos.x(), px.y() - pos.y() );
        if ( distance < bestDistance )
        {
            bestDistance = distance;
            bestDays = mRawT[i];
            bestValue = mRawV[i];
        }
    }
    if ( bestDistance <= kHoverSnapPixels )
        emit sampleHovered( bestDays, bestValue );
}

void TemporalProfileWidget::resizeEvent( QResizeEvent * )
{
    mBackgroundDirty = true;
}

// --- TemporalTimelineWidget ------------------------------------------------

TemporalTimelineWidget::TemporalTimelineWidget( QWidget *parent )
  : QWidget( parent )
{
    mLayout = new QVBoxLayout( this );
    mLayout->setContentsMargins( 0, 0, 0, 0 );
    mProfile = new TemporalProfileWidget( this );
    mScrubber = new TimelineScrubberWidget( this );
    mLayout->addWidget( mProfile, 1 );
    mLayout->addWidget( mScrubber );

    // Slice changes drive the profile's hover cursor.
    connect( mScrubber, &TimelineScrubberWidget::dateChanged, this,
             [this]( int index, const QString &isoDate ) { onSliceChanged( index, isoDate ); } );
}

void TemporalTimelineWidget::setTimeline( const std::vector<QString> &isoDates,
                                          const std::vector<double> &sliceDays )
{
    mSliceDays = sliceDays;
    mScrubber->setTimelineDates( isoDates );
}

void TemporalTimelineWidget::onSliceChanged( int index, const QString &isoDate )
{
    Q_UNUSED( isoDate );
    if ( index >= 0 && static_cast<std::size_t>( index ) < mSliceDays.size() )
        mProfile->setScrubberHoverDate( mSliceDays[static_cast<std::size_t>( index )] );
    else
        mProfile->setScrubberHoverDate( std::numeric_limits<double>::quiet_NaN() );
}

} // namespace sicnu::gui
