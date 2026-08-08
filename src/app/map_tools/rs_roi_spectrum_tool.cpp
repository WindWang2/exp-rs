// src/app/map_tools/rs_roi_spectrum_tool.cpp — polygon ROI mean-spectrum tool
#include "rs_roi_spectrum_tool.h"

#include "processing/algorithms/spectral_roi.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <qgscoordinatetransform.h>
#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgsmaptopixel.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include <cmath>

RsRoiSpectrumTool::RsRoiSpectrumTool( QgsMapCanvas *canvas, QgsRasterLayer *rasterLayer,
                                      ResultCallback onResult )
  : QgsMapTool( canvas )
  , m_rasterLayer( rasterLayer )
  , m_onResult( std::move( onResult ) )
{
  m_rubberBand = std::make_unique<QgsRubberBand>( canvas, Qgis::GeometryType::Polygon );
  m_rubberBand->setColor( QColor( 255, 120, 0, 100 ) );
  m_rubberBand->setWidth( 2 );
}

RsRoiSpectrumTool::~RsRoiSpectrumTool() = default;

void RsRoiSpectrumTool::canvasPressEvent( QgsMapMouseEvent *e )
{
  if ( e->button() != Qt::LeftButton )
  {
    if ( e->button() == Qt::RightButton && m_polygon.size() >= 3 )
      finishPolygon();
    return;
  }
  if ( m_polygon.isEmpty() )
  {
    m_rubberBand->reset( Qgis::GeometryType::Polygon );
  }
  const QgsPointXY mapPoint = toMapCoordinates( e->pos() );
  m_polygon << QPointF( mapPoint.x(), mapPoint.y() );
  m_rubberBand->addPoint( mapPoint );
}

void RsRoiSpectrumTool::canvasMoveEvent( QgsMapMouseEvent *e )
{
  if ( !m_polygon.isEmpty() )
    m_rubberBand->movePoint( toMapCoordinates( e->pos() ) );
}

void RsRoiSpectrumTool::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  Q_UNUSED( e )
  // Polygon is accumulated on press events; a release with >= 3 vertices
  // (double-click / right-click handled in press) closes it.
  if ( m_polygon.size() >= 3 )
    finishPolygon();
}

void RsRoiSpectrumTool::finishPolygon()
{
  if ( m_finished )
    return;
  m_finished = true;

  if ( m_rasterLayer.isNull() || !m_rasterLayer->isValid()
       || m_polygon.size() < 3 )
  {
    if ( m_onResult )
      m_onResult( {}, {}, {},
                  tr( "ROI 需要至少 3 个点且栅格图层有效。" ) );
    return;
  }

  // The ROI kernel expects the polygon in the raster's map CRS; transform the
  // canvas-drawn polygon when the canvas CRS differs.
  QPolygonF roi = m_polygon;
  const QgsCoordinateReferenceSystem canvasCrs =
    mCanvas ? mCanvas->mapSettings().destinationCrs() : QgsCoordinateReferenceSystem();
  if ( canvasCrs.isValid() && canvasCrs != m_rasterLayer->crs() )
  {
    const QgsCoordinateTransform transform( canvasCrs, m_rasterLayer->crs(),
                                            QgsProject::instance()->transformContext() );
    QPolygonF transformed;
    for ( const QPointF &p : m_polygon )
      transformed << transform.transform( QgsPointXY( p.x(), p.y() ) ).toQPointF();
    roi = transformed;
  }

  SpectralRoiProfile::RoiProfileResult result;
  QString errorMessage;
  if ( !SpectralRoiProfile::meanSpectrum( m_rasterLayer->source(), roi,
                                          &result, &errorMessage ) )
  {
    // Surface the kernel's actionable error instead of vanishing silently.
    if ( m_onResult )
      m_onResult( {}, {}, {}, errorMessage );
    return;
  }

  QVector<double> values;
  values.reserve( result.mean.size() );
  for ( float v : result.mean )
    values.append( v );

  QVector<double> wavelengths;
  wavelengths.reserve( result.wavelengths.size() );
  for ( float w : result.wavelengths )
    wavelengths.append( w );

  // Per-band labels: band description when present, else "波段 N".
  QVector<QString> labels;
  GdalDatasetWrapper ds;
  const bool hasDs = ds.open( m_rasterLayer->source() );
  for ( int b = 1; b <= values.size(); ++b )
  {
    QString label;
    if ( hasDs )
      label = ds.bandDescription( b );
    if ( label.isEmpty() )
      label = tr( "波段 %1" ).arg( b );
    labels.append( label );
  }

  if ( m_onResult )
    m_onResult( values, wavelengths, labels, m_rasterLayer->name() );
}
