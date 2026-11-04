// cross_section_widget.h — Cross-Section Profile Widget
#pragma once

#include <QPointer>
#include <QWidget>
#include <QVector>

class QgsRasterLayer;
class QgsPointXY;

/**
 * Widget for displaying cross-section profiles along a line.
 * Shows pixel values along a user-defined transect.
 */
class CrossSectionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CrossSectionWidget(QWidget *parent = nullptr);
    ~CrossSectionWidget() override;

    /** Set the raster layer for analysis. */
    void setRasterLayer(QgsRasterLayer *layer);

    /** Extract profile along a line from start to end point. */
    void extractProfile(const QgsPointXY &start, const QgsPointXY &end);

    /** Clear the current profile. */
    void clear();

    /// Read-only projections of the current profile (test seam, #1790).
    bool hasData() const { return m_hasData; }
    const QVector<double> &values() const { return m_values; }
    const QVector<double> &distances() const { return m_distances; }

    QSize minimumSizeHint() const override { return QSize(320, 200); }
    QSize sizeHint() const override { return QSize(500, 300); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawChart(QPainter &painter, const QRect &chartRect);
    void drawAxes(QPainter &painter, const QRect &chartRect);
    /// GUI-thread landing for the async sampling (#1790): drops superseded
    /// results (requestId mismatch) and repaints the chart.
    void landProfile(quint64 requestId, const QVector<double> &values,
                     const QVector<double> &distances);

    // QPointer so the reference nulls automatically when the layer is removed
    // from the project (perf/architecture goal §13: project-close / layer-remove
    // lifetime safety). The extractProfile/paint paths already null-check.
    QPointer<QgsRasterLayer> m_rasterLayer;
    QVector<double> m_values;
    QVector<double> m_distances;
    QString m_layerName;
    bool m_hasData = false;
    double m_minValue = 0.0;
    double m_maxValue = 0.0;
    /// Async sampling state (#1790): every extractProfile bump supersedes
    /// in-flight work; the landing is dropped unless the id still matches.
    quint64 m_requestEpoch = 0;
    /// RsScanPool cancellation generation for the in-flight sampling (0 =
    /// none). Cancelled in the destructor so a dying widget stops the work.
    quint64 m_scanGeneration = 0;
};
