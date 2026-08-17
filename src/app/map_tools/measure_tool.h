#pragma once

#include <qgsmaptool.h>
#include <qgspointxy.h>
#include <qgsdistancearea.h>

#include <QVector>

class QgsMapCanvas;
class QgsRubberBand;

/**
 * \brief Map tool for measuring distance (polyline) or area (polygon) on the canvas.
 *
 * Left-click adds vertices. Double-click or right-click finishes the measurement.
 * Pressing Escape cancels the current measurement.
 */
class MeasureTool : public QgsMapTool
{
    Q_OBJECT

  public:
    enum MeasureMode
    {
        Distance,
        Area
    };

    explicit MeasureTool( QgsMapCanvas *canvas, MeasureMode mode, QObject *parent = nullptr );
    ~MeasureTool() override;

    MeasureMode mode() const { return mMode; }

    void canvasPressEvent( QgsMapMouseEvent *e ) override;
    void canvasMoveEvent( QgsMapMouseEvent *e ) override;
    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;
    void canvasDoubleClickEvent( QgsMapMouseEvent *e ) override;
    void keyPressEvent( QKeyEvent *e ) override;
    void activate() override;
    void deactivate() override;

    void updateDistanceArea();
    const QgsDistanceArea &distanceArea() const { return mDistanceArea; }
    const QVector<QgsPointXY> &points() const { return mPoints; }

  signals:
    void measurementComplete( double value, const QString &unit );

  private:
    void finishMeasurement();
    void reset();

    MeasureMode mMode;
    QgsRubberBand *mRubberBand = nullptr;
    QVector<QgsPointXY> mPoints;
    QgsDistanceArea mDistanceArea;
};
