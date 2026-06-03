#include "rs_spectral_curve_widget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>

RsSpectralCurveWidget::RsSpectralCurveWidget( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsSpectralCurve" ) );
  setMinimumSize( 360, 140 );
}

void RsSpectralCurveWidget::setClassCurves( const QVector<Curve> &curves )
{
  mCurves = curves;
  update();
}

void RsSpectralCurveWidget::setSelectedClass( int classId )
{
  if ( mSelected == classId )
    return;
  mSelected = classId;
  update();
}

void RsSpectralCurveWidget::paintEvent( QPaintEvent * )
{
  QPainter p( this );
  p.setRenderHint( QPainter::Antialiasing );

  // Background
  p.fillRect( rect(), QColor( "#ffffff" ) );

  if ( mCurves.isEmpty() )
  {
    p.setPen( QColor( "#5f6b7a" ) );
    p.drawText( rect(), Qt::AlignCenter,
                tr( "Select a class to view spectral curves" ) );
    return;
  }

  const int margin = 30;
  QRectF plot = rect().adjusted( margin, margin / 2, -margin / 2, -margin );

  // Axes (left + bottom) in light grey.
  p.setPen( QColor( "#e6eaef" ) );
  p.drawLine( plot.topLeft(), plot.bottomLeft() );
  p.drawLine( plot.bottomLeft(), plot.bottomRight() );

  // Determine global y range across all curves (mean ± σ envelope) and
  // the maximum band count for x-axis scaling.
  double ymin = 1e18;
  double ymax = -1e18;
  int nBands = 0;
  for ( const Curve &c : mCurves )
  {
    nBands = std::max( nBands, static_cast<int>( c.bandMeans.size() ) );
    for ( int i = 0; i < c.bandMeans.size(); ++i )
    {
      const double s = ( i < c.bandStds.size() ) ? c.bandStds[i] : 0.0;
      const double lo = c.bandMeans[i] - s;
      const double hi = c.bandMeans[i] + s;
      ymin = std::min( ymin, lo );
      ymax = std::max( ymax, hi );
    }
  }
  if ( nBands < 2 || ymax <= ymin )
    return;

  // Pad y range by 10% so the curves never touch the top/bottom edges.
  const double pad = ( ymax - ymin ) * 0.1;
  ymin -= pad;
  ymax += pad;

  auto xAt = [&]( int band ) {
    return plot.left() + plot.width() * band / double( nBands - 1 );
  };
  auto yAt = [&]( double v ) {
    return plot.bottom() - plot.height() * ( v - ymin ) / ( ymax - ymin );
  };

  for ( const Curve &c : mCurves )
  {
    // ±σ shaded envelope.
    QColor bandColor = c.color;
    bandColor.setAlpha( 50 );

    QPolygonF upper;
    QPolygonF lower;
    for ( int i = 0; i < c.bandMeans.size(); ++i )
    {
      const double s = ( i < c.bandStds.size() ) ? c.bandStds[i] : 0.0;
      upper.append( QPointF( xAt( i ), yAt( c.bandMeans[i] + s ) ) );
      lower.append( QPointF( xAt( i ), yAt( c.bandMeans[i] - s ) ) );
    }
    QPolygonF fill = upper;
    for ( int i = lower.size() - 1; i >= 0; --i )
      fill.append( lower[i] );
    p.setBrush( bandColor );
    p.setPen( Qt::NoPen );
    p.drawPolygon( fill );

    // Mean polyline — selected class drawn thicker.
    const double width = ( c.classId == mSelected ) ? 2.5 : 1.5;
    QPen pen( c.color, width );
    p.setPen( pen );
    p.setBrush( Qt::NoBrush );
    QPolygonF line;
    for ( int i = 0; i < c.bandMeans.size(); ++i )
      line.append( QPointF( xAt( i ), yAt( c.bandMeans[i] ) ) );
    p.drawPolyline( line );
  }
}
