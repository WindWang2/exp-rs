// rs_segment_info_dock.cpp — Phase 10B Task 10B.5
#include "rs_segment_info_dock.h"

RsSegmentInfoDock::RsSegmentInfoDock( QWidget *parent )
    : QgsDockWidget( tr( "Segment Info" ), parent )
{
    mInfoText = new QTextEdit;
    mInfoText->setReadOnly( true );
    mInfoText->setMinimumHeight( 150 );
    setWidget( mInfoText );
}

void RsSegmentInfoDock::showSegmentInfo( quint32 segmentId,
                                          const RsSegmentFeatures::SegmentStat &stat,
                                          int classId )
{
    QString html;
    html += QStringLiteral( "<b>%1 %2</b><br>" ).arg( tr( "Segment" ) ).arg( segmentId );
    if ( classId > 0 )
        html += QStringLiteral( "%1 %2<br>" ).arg( tr( "Class:" ) ).arg( classId );

    html += QStringLiteral( "<br><b>%1</b><br>" ).arg( tr( "Shape:" ) );
    html += QStringLiteral( "%1: %2<br>" ).arg( tr( "Area (pixels)" ) ).arg( stat.area, 0, 'f', 0 );
    html += QStringLiteral( "%1: %2<br>" ).arg( tr( "Perimeter" ) ).arg( stat.perimeter, 0, 'f', 0 );
    html += QStringLiteral( "%1: %2<br>" ).arg( tr( "Shape Index" ) ).arg( stat.shapeIndex, 0, 'f', 3 );

    html += QStringLiteral( "<br><b>%1</b><br>" ).arg( tr( "Spectral (per band):" ) );
    html += QStringLiteral( "<table border='1' cellpadding='2'>" );
    html += QStringLiteral( "<tr><th>Band</th><th>Mean</th><th>StdDev</th><th>Min</th><th>Max</th></tr>" );
    for ( int b = 0; b < stat.mean.size(); ++b )
    {
        html += QStringLiteral( "<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>" )
                    .arg( b + 1 )
                    .arg( stat.mean[b], 0, 'f', 2 )
                    .arg( stat.stddev[b], 0, 'f', 2 )
                    .arg( stat.min[b], 0, 'f', 2 )
                    .arg( stat.max[b], 0, 'f', 2 );
    }
    html += QStringLiteral( "</table>" );

    mInfoText->setHtml( html );
}

void RsSegmentInfoDock::clearInfo()
{
    mInfoText->clear();
}
