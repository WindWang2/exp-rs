#include "rs_jm_matrix_widget.h"

#include <QFont>
#include <QPainter>
#include <QRect>
#include <QString>

#include <algorithm>

RsJmMatrixWidget::RsJmMatrixWidget( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsJmMatrix" ) );
  setMinimumSize( 280, 260 );
}

void RsJmMatrixWidget::setData( const QVector<ClassEntry> &classes,
                                const QHash<QPair<int, int>, double> &jm )
{
  mClasses = classes;
  mJm = jm;
  update();
}

void RsJmMatrixWidget::paintEvent( QPaintEvent * )
{
  QPainter p( this );
  p.fillRect( rect(), Qt::white );
  const int n = mClasses.size();
  if ( n == 0 )
    return;

  const int labelW = 60;
  int cell = std::min( ( width() - labelW ) / n, ( height() - labelW ) / n );
  if ( cell < 12 )
    cell = 12;
  const QPoint origin( labelW, labelW );
  p.setFont( QFont( QStringLiteral( "IBM Plex Mono" ), 9 ) );

  // Color thresholds match the design.html ArtboardClassify swatches.
  auto colorFor = []( double jm ) -> QColor {
    if ( jm >= 1.9 )
      return QColor( QStringLiteral( "#2da44e" ) );
    if ( jm >= 1.5 )
      return QColor( QStringLiteral( "#a3e635" ) );
    if ( jm >= 1.0 )
      return QColor( QStringLiteral( "#d29922" ) );
    return QColor( QStringLiteral( "#cf222e" ) );
  };

  // Row labels (right-aligned) and rotated column labels.
  for ( int i = 0; i < n; ++i )
  {
    p.setPen( Qt::black );
    p.drawText( QRect( 2, origin.y() + i * cell, labelW - 4, cell ),
                Qt::AlignVCenter | Qt::AlignRight, mClasses[i].name );
    p.save();
    p.translate( origin.x() + i * cell + cell / 2, labelW - 2 );
    p.rotate( -45 );
    p.drawText( QRect( -labelW, -8, labelW, 14 ),
                Qt::AlignVCenter | Qt::AlignRight, mClasses[i].name );
    p.restore();
  }

  for ( int r = 0; r < n; ++r )
  {
    for ( int c = 0; c < n; ++c )
    {
      const QRect cellRect( origin.x() + c * cell, origin.y() + r * cell, cell, cell );
      if ( r == c )
      {
        p.fillRect( cellRect, QColor( QStringLiteral( "#f1f3f6" ) ) );
        p.setPen( Qt::black );
        p.drawText( cellRect, Qt::AlignCenter, QStringLiteral( "—" ) );
      }
      else
      {
        const int a = std::min( mClasses[r].id, mClasses[c].id );
        const int b = std::max( mClasses[r].id, mClasses[c].id );
        const double jm = mJm.value( qMakePair( a, b ), -1.0 );
        if ( jm < 0 )
        {
          // No data yet for this pair — render as a near-white placeholder.
          p.fillRect( cellRect, QColor( QStringLiteral( "#fafbfc" ) ) );
        }
        else
        {
          p.fillRect( cellRect, colorFor( jm ) );
          p.setPen( Qt::black );
          p.drawText( cellRect, Qt::AlignCenter, QString::number( jm, 'f', 2 ) );
        }
      }
      p.setPen( QColor( QStringLiteral( "#e6eaef" ) ) );
      p.drawRect( cellRect );
    }
  }
}
