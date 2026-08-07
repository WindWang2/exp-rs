// rs_roi_spectrum_tool.h — polygon ROI mean-spectrum map tool
#pragma once

#include <qgsmaptool.h>
#include <qgsrubberband.h>

#include <QPolygonF>
#include <QVector>

#include <functional>

class QgsMapCanvas;
class QgsRasterLayer;

/**
 * Polygon ROI spectrum tool: the user traces a polygon on the canvas; on
 * release the tool computes the ROI mean spectrum over the raster
 * (SpectralRoiProfile kernel, ADR-0084) and reports it through the callback
 * (values + wavelengths + labels). The tool then schedules its own deletion;
 * the caller should restore the previous map tool in the callback.
 */
class RsRoiSpectrumTool : public QgsMapTool
{
    Q_OBJECT

  public:
    using ResultCallback = std::function<void( const QVector<double> &values,
                                               const QVector<double> &wavelengths,
                                               const QVector<QString> &labels,
                                               const QString &layerName )>;

    RsRoiSpectrumTool( QgsMapCanvas *canvas, QgsRasterLayer *rasterLayer,
                       ResultCallback onResult );
    ~RsRoiSpectrumTool() override;

  protected:
    void canvasPressEvent( QgsMapMouseEvent *e ) override;
    void canvasMoveEvent( QgsMapMouseEvent *e ) override;
    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;

  private:
    void finishPolygon();

    QgsRasterLayer *m_rasterLayer = nullptr;
    ResultCallback m_onResult;
    std::unique_ptr<QgsRubberBand> m_rubberBand;
    QPolygonF m_polygon;
};
