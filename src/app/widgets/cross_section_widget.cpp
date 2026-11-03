// cross_section_widget.cpp — Cross-Section Profile Widget
#include "cross_section_widget.h"
#include "rs_scan_pool.h"
#include "core/sicnu_logging.h"

#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgspointxy.h>

#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QFont>
#include <QFontMetrics>

#include <gdal.h>
#include <cmath>

CrossSectionWidget::CrossSectionWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

CrossSectionWidget::~CrossSectionWidget()
{
    // #1790: stop the in-flight sampling when the widget dies — the landing
    // is dropped by the QPointer anyway; this ends the work itself (#797).
    if (m_scanGeneration)
        sicnu::app::RsScanPool::instance().cancel(m_scanGeneration, this);
}

void CrossSectionWidget::setRasterLayer(QgsRasterLayer *layer)
{
    // A layer switch invalidates any sampling still in flight for the old
    // source: its landing must not repaint over the new layer's state.
    ++m_requestEpoch;
    m_rasterLayer = layer;
    clear();
}

void CrossSectionWidget::extractProfile(const QgsPointXY &start, const QgsPointXY &end)
{
    // #1790: this used to GDALOpen + per-sample GDALRasterIO on the GUI
    // thread — a network-backed or long transect froze the event loop for
    // the whole read. The sampling now runs on the bounded scan pool
    // (#797, RoiStatisticsWidget pattern): plain values cross the thread
    // boundary, the result lands through the application event loop, and a
    // newer request (or widget death) drops it.
    if (!m_rasterLayer || !m_rasterLayer->dataProvider())
        return;

    const QString source = m_rasterLayer->source();
    if (source.isEmpty())
        return;

    m_values.clear();
    m_distances.clear();
    m_hasData = false;
    m_layerName = m_rasterLayer->name();
    update();

    const quint64 requestId = ++m_requestEpoch;
    const quint64 scanGeneration =
        sicnu::app::RsScanPool::instance().nextGeneration(this);
    m_scanGeneration = scanGeneration;
    QPointer<CrossSectionWidget> self(this);
    // Owner key captured on the GUI thread (isStale's map is keyed by raw
    // address — reading QPointer internals off-thread would race).
    const void *ownerKey = static_cast<const void *>(this);

    sicnu::app::RsScanPool::instance().pool().start(
        [self, ownerKey, source, start, end, requestId, scanGeneration]() {
            QVector<double> values;
            QVector<double> distances;

            GDALDatasetH ds = GDALOpen(source.toUtf8().constData(), GA_ReadOnly);
            if (ds)
            {
                GDALRasterBandH band = GDALGetRasterBand(ds, 1);
                if (band)
                {
                    double gt[6];
                    GDALGetGeoTransform(ds, gt);
                    double invGt[6];
                    if (GDALInvGeoTransform(gt, invGt))
                    {
                        const double sx = invGt[0] + start.x() * invGt[1] + start.y() * invGt[2];
                        const double sy = invGt[3] + start.x() * invGt[4] + start.y() * invGt[5];
                        const double ex = invGt[0] + end.x() * invGt[1] + end.y() * invGt[2];
                        const double ey = invGt[3] + end.x() * invGt[4] + end.y() * invGt[5];

                        const double dx = ex - sx;
                        const double dy = ey - sy;
                        const double length = std::sqrt(dx * dx + dy * dy);
                        const int steps = std::max(2, static_cast<int>(length));

                        values.resize(steps);
                        distances.resize(steps);

                        const double totalDist =
                            std::sqrt((end.x() - start.x()) * (end.x() - start.x()) +
                                      (end.y() - start.y()) * (end.y() - start.y()));
                        const int W = GDALGetRasterXSize(ds);
                        const int H = GDALGetRasterYSize(ds);

                        for (int i = 0; i < steps; ++i)
                        {
                            // Cooperative cancellation every 16 samples
                            // (review S2/S3): on a remote COG each sample
                            // can be its own range request — a superseded
                            // request or widget death must stop the reads
                            // quickly, not after 128 network round trips.
                            if ((i & 15) == 0 &&
                                sicnu::app::RsScanPool::instance().isStale(scanGeneration, ownerKey))
                            {
                                GDALClose(ds);
                                return;
                            }
                            const double t = static_cast<double>(i) / (steps - 1);
                            const int ix = static_cast<int>(sx + t * dx);
                            const int iy = static_cast<int>(sy + t * dy);
                            if (ix >= 0 && ix < W && iy >= 0 && iy < H)
                            {
                                double val = 0.0;
                                GDALRasterIO(band, GF_Read, ix, iy, 1, 1, &val, 1, 1,
                                             GDT_Float64, 0, 0);
                                values[i] = val;
                            }
                            else
                            {
                                values[i] = std::numeric_limits<double>::quiet_NaN();
                            }
                            distances[i] = t * totalDist;
                        }
                    }
                }
                GDALClose(ds);
            }

            // One marshalled hop onto the GUI thread; dropped when the
            // widget (or its request) is already gone.
            QMetaObject::invokeMethod(qApp, [self, requestId, values, distances]() {
                if (self)
                    self->landProfile(requestId, values, distances);
            });
        });
}

void CrossSectionWidget::landProfile(quint64 requestId, const QVector<double> &values,
                                     const QVector<double> &distances)
{
    if (requestId != m_requestEpoch)
        return; // superseded by a newer extractProfile / layer switch

    m_values = values;
    m_distances = distances;

    m_minValue = std::numeric_limits<double>::max();
    m_maxValue = std::numeric_limits<double>::lowest();
    for (double v : m_values)
    {
        if (std::isnan(v))
            continue;
        m_minValue = std::min(m_minValue, v);
        m_maxValue = std::max(m_maxValue, v);
    }

    m_hasData = !m_values.isEmpty();
    update();

    SICNU_LOG_INFO(SicnuLogTags::Widgets,
                   QString("Cross-section profile extracted: %1 points").arg(m_values.size()));
}

void CrossSectionWidget::clear()
{
    ++m_requestEpoch;
    m_values.clear();
    m_distances.clear();
    m_hasData = false;
    update();
}

void CrossSectionWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(250, 250, 252));

    if (!m_hasData) {
        painter.setPen(QColor(160, 160, 160));
        painter.setFont(QFont("sans-serif", 11));
        painter.drawText(rect(), Qt::AlignCenter,
                         m_rasterLayer
                             ? tr("Click two points on map to create cross-section")
                             : tr("Select a raster layer first"));
        return;
    }

    QRect chartRect(60, 30, width() - 80, height() - 60);
    drawChart(painter, chartRect);
}

void CrossSectionWidget::drawChart(QPainter &painter, const QRect &chartRect)
{
    // Background
    painter.fillRect(chartRect, QColor(255, 255, 255));
    painter.setPen(QPen(QColor(200, 200, 200), 1));
    painter.drawRect(chartRect.adjusted(0, 0, -1, -1));

    // Grid lines
    painter.setPen(QPen(QColor(230, 230, 230), 1, Qt::DashLine));
    for (int i = 1; i <= 4; ++i) {
        int y = chartRect.top() + chartRect.height() * i / 5;
        painter.drawLine(chartRect.left(), y, chartRect.right(), y);
    }

    drawAxes(painter, chartRect);

    // Draw profile line
    if (m_values.size() < 2) return;

    double valueRange = m_maxValue - m_minValue;
    if (valueRange <= 0.0) return;

    QVector<QPoint> points;
    for (int i = 0; i < m_values.size(); ++i) {
        double xFrac = m_distances[i] / m_distances.last();
        double yFrac = (m_values[i] - m_minValue) / valueRange;

        int px = chartRect.left() + static_cast<int>(xFrac * chartRect.width());
        int py = chartRect.bottom() - static_cast<int>(yFrac * chartRect.height());
        points.append(QPoint(px, py));
    }

    painter.setPen(QPen(QColor(66, 133, 244), 2));
    for (int i = 0; i < points.size() - 1; ++i) {
        painter.drawLine(points[i], points[i + 1]);
    }

    // Title
    painter.setPen(QColor(50, 50, 50));
    painter.setFont(QFont("sans-serif", 10, QFont::Bold));
    painter.drawText(QRect(60, 0, width() - 80, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     tr("Cross-Section — %1").arg(m_layerName));
}

void CrossSectionWidget::drawAxes(QPainter &painter, const QRect &chartRect)
{
    painter.setPen(QColor(80, 80, 80));
    painter.setFont(QFont("sans-serif", 8));

    // X axis labels (distance)
    for (int i = 0; i <= 4; ++i) {
        double dist = m_distances.last() * i / 4;
        int x = chartRect.left() + chartRect.width() * i / 4;
        painter.drawText(x - 15, chartRect.bottom() + 15, QString::number(dist, 'f', 1));
    }

    // Y axis labels (value)
    for (int i = 0; i <= 4; ++i) {
        double val = m_minValue + (m_maxValue - m_minValue) * i / 4;
        int y = chartRect.bottom() - chartRect.height() * i / 4;
        painter.drawText(chartRect.left() - 45, y + 4, QString::number(val, 'f', 1));
    }

    // Axis titles
    painter.setFont(QFont("sans-serif", 9));
    painter.drawText(chartRect.left() + chartRect.width() / 2 - 20, height() - 5, tr("Distance"));
}
