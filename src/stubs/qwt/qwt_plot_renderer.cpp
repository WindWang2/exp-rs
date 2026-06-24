// qwt_plot_renderer.cpp - QPainter-based rendering for QwtPlot stubs
#include "qwt_plot_renderer.h"
#include "qwt_plot.h"
#include "qwt_plot_histogram.h"
#include "qwt_plot_curve.h"
#include "qwt_scale_map.h"

#include <QPainter>
#include <QRectF>
#include <QPen>
#include <QBrush>

void QwtPlotRenderer::renderPlot(QwtPlot *plot, QPainter *painter, const QRectF &rect) const
{
    if (!plot || !painter) return;

    painter->save();

    // Set up coordinate maps
    QwtScaleMap xMap, yMap;
    xMap.setPaintInterval(rect.left(), rect.right());
    yMap.setPaintInterval(rect.bottom(), rect.top()); // Y axis is inverted

    // Find data bounds
    double xMin = 1e30, xMax = -1e30;
    double yMin = 0, yMax = 1e30;

    // Look for histogram items
    const auto children = plot->findChildren<QwtPlotHistogram *>();
    for (const auto *histogram : children) {
        if (!histogram->isVisible()) continue;
        const auto &samples = histogram->samples();
        for (const auto &sample : samples) {
            xMin = qMin(xMin, sample.interval.minValue());
            xMax = qMax(xMax, sample.interval.maxValue());
            yMax = qMax(yMax, sample.value);
        }
    }

    // Look for curve items
    const auto curves = plot->findChildren<QwtPlotCurve *>();
    for (const auto *curve : curves) {
        if (!curve->isVisible()) continue;
        const auto &xData = curve->xData();
        const auto &yData = curve->yData();
        for (int i = 0; i < xData.size(); ++i) {
            xMin = qMin(xMin, xData[i]);
            xMax = qMax(xMax, xData[i]);
            if (i < yData.size()) {
                yMax = qMax(yMax, yData[i]);
            }
        }
    }

    // Set scale intervals
    if (xMin < xMax) {
        xMap.setScaleInterval(xMin, xMax);
    }
    yMap.setScaleInterval(yMin, yMax);

    // Render histograms
    for (const auto *histogram : children) {
        if (!histogram->isVisible()) continue;
        renderHistogram(const_cast<QwtPlotHistogram *>(histogram), painter, xMap, yMap, rect);
    }

    // Render curves
    for (const auto *curve : curves) {
        if (!curve->isVisible()) continue;
        renderCurve(const_cast<QwtPlotCurve *>(curve), painter, xMap, yMap);
    }

    painter->restore();
}

void QwtPlotRenderer::renderHistogram(QwtPlotHistogram *histogram, QPainter *painter,
                                       const QwtScaleMap &xMap, const QwtScaleMap &yMap,
                                       const QRectF &canvasRect) const
{
    if (!histogram || !painter) return;

    const auto &samples = histogram->samples();
    if (samples.isEmpty()) return;

    painter->save();

    QPen pen = histogram->pen();
    QBrush brush = histogram->brush();
    if (brush.style() == Qt::NoBrush) {
        brush = QBrush(QColor(70, 130, 180)); // Steel blue default
    }
    if (pen.style() == Qt::NoPen) {
        pen = QPen(Qt::black, 1);
    }

    painter->setPen(pen);
    painter->setBrush(brush);

    double baseline = histogram->baseline();
    double baselineY = yMap.transform(baseline);

    for (const auto &sample : samples) {
        double x1 = xMap.transform(sample.interval.minValue());
        double x2 = xMap.transform(sample.interval.maxValue());
        double y = yMap.transform(sample.value);

        QRectF bar(qMin(x1, x2), qMin(y, baselineY),
                   qAbs(x2 - x1), qAbs(y - baselineY));

        // Ensure minimum bar width for visibility
        if (bar.width() < 1.0) {
            bar.setWidth(1.0);
        }

        painter->drawRect(bar);
    }

    painter->restore();
}

void QwtPlotRenderer::renderCurve(QwtPlotCurve *curve, QPainter *painter,
                                   const QwtScaleMap &xMap, const QwtScaleMap &yMap) const
{
    if (!curve || !painter) return;

    const auto &xData = curve->xData();
    const auto &yData = curve->yData();

    if (xData.isEmpty() || yData.isEmpty()) return;
    if (xData.size() != yData.size()) return;

    painter->save();

    QPen pen = curve->pen();
    if (pen.style() == Qt::NoPen) {
        pen = QPen(Qt::darkGray, 1);
    }
    painter->setPen(pen);

    QPolygonF points;
    for (int i = 0; i < xData.size(); ++i) {
        double x = xMap.transform(xData[i]);
        double y = yMap.transform(yData[i]);
        points << QPointF(x, y);
    }

    if (curve->style() == QwtPlotCurve::Lines && points.size() > 1) {
        painter->drawPolyline(points);
    } else if (curve->style() == QwtPlotCurve::Dots) {
        for (const auto &point : points) {
            painter->drawPoint(point);
        }
    } else if (curve->style() == QwtPlotCurve::Sticks) {
        for (const auto &point : points) {
            double baselineY = yMap.transform(0);
            painter->drawLine(QPointF(point.x(), baselineY), point);
        }
    }

    painter->restore();
}
