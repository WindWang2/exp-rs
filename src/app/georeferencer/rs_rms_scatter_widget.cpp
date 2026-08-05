#include "rs_rms_scatter_widget.h"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <cmath>

RsRmsScatterWidget::RsRmsScatterWidget( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsRmsScatter" ) );
  setMinimumSize( 150, 150 );
}

void RsRmsScatterWidget::setResiduals( const QVector<QPointF> &v )
{
  mPts = v;
  update();
}

void RsRmsScatterWidget::setWarnThreshold( double pixels )
{
  if ( pixels <= 0.0 )
    return;
  mWarnPx = pixels;
  update();
}

void RsRmsScatterWidget::paintEvent( QPaintEvent * )
{
  QPainter p( this );
  p.setRenderHint( QPainter::Antialiasing );

  const int W = width();
  const int H = height();
  const int cx = W / 2;
  const int cy = H / 2;
  const int R = (std::min)( W, H ) / 2 - 4;

  // White background
  p.fillRect( rect(), QColor( "#ffffff" ) );

  // Light grid: horizontal + vertical axis lines through centre
  p.setPen( QPen( QColor( "#e6eaef" ), 1 ) );
  p.drawLine( 0, cy, W, cy );
  p.drawLine( cx, 0, cx, H );

  // Map residual magnitude in px to widget pixels so the warn-circle
  // sits well inside the viewport.
  const double scale = static_cast<double>( R ) / (std::max)( 1.5, mWarnPx * 1.4 );

  // Dashed warn-circle at threshold
  p.setPen( QPen( QColor( "#bf8700" ), 1, Qt::DashLine ) );
  p.setBrush( Qt::NoBrush );
  const int warnR = (std::min)( R, static_cast<int>( mWarnPx * scale ) );
  p.drawEllipse( QPoint( cx, cy ), warnR, warnR );

  // Dots
  for ( const QPointF &pt : mPts )
  {
    const double r = std::hypot( pt.x(), pt.y() );
    const QColor c = ( r >= mWarnPx ) ? QColor( "#bf8700" ) : QColor( "#208830" );
    p.setBrush( c );
    p.setPen( Qt::NoPen );
    const int x = cx + static_cast<int>( pt.x() * scale );
    const int y = cy + static_cast<int>( pt.y() * scale );
    p.drawEllipse( QPoint( x, y ), 3, 3 );
  }
}
