// rs_roi_spectrum_tool.h — polygon ROI mean-spectrum map tool
#pragma once

#include <qgsmaptool.h>
#include <qgsrubberband.h>

#include <QPointer>
#include <QPolygonF>
#include <QVector>

#include <functional>

class QgsMapCanvas;
class QgsRasterLayer;

/**
 * Polygon ROI spectrum tool: the user traces a polygon on the canvas; on
 * release the tool computes the ROI mean spectrum over the raster
 * (SpectralRoiProfile kernel, ADR-0084) and reports it through the callback.
 *
 * Callback contract: called exactly once on every finish — with
 * (values, wavelengths, labels, layerName) on success, or with empty values
 * and the error message in @p layerName on failure. The caller is the sole
 * owner: it must restore the previous map tool and delete the tool via
 * release()->deleteLater() inside the callback.
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
    void canvasDoubleClickEvent( QgsMapMouseEvent *e ) override;

  private:
    void finishPolygon();

    QPointer<QgsRasterLayer> m_rasterLayer;
    ResultCallback m_onResult;
    std::unique_ptr<QgsRubberBand> m_rubberBand;
    QPolygonF m_polygon;
    bool m_finished = false;
};
