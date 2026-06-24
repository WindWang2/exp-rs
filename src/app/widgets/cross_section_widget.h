// cross_section_widget.h — Cross-Section Profile Widget
#pragma once

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

    /** Set the raster layer for analysis. */
    void setRasterLayer(QgsRasterLayer *layer);

    /** Extract profile along a line from start to end point. */
    void extractProfile(const QgsPointXY &start, const QgsPointXY &end);

    /** Clear the current profile. */
    void clear();

    QSize minimumSizeHint() const override { return QSize(320, 200); }
    QSize sizeHint() const override { return QSize(500, 300); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawChart(QPainter &painter, const QRect &chartRect);
    void drawAxes(QPainter &painter, const QRect &chartRect);

    QgsRasterLayer *m_rasterLayer = nullptr;
    QVector<double> m_values;
    QVector<double> m_distances;
    QString m_layerName;
    bool m_hasData = false;
    double m_minValue = 0.0;
    double m_maxValue = 0.0;
};
