#pragma once
// QwtPlotRenderer - QPainter-based rendering for QwtPlot
#include <QObject>
#include <QPainter>
#include <QRectF>
#include "qwt_global.h"
#include "qwt_plot.h"
#include "qwt_plot_histogram.h"
#include "qwt_plot_curve.h"
#include "qwt_scale_map.h"

class QPrinter;

class QWT_EXPORT QwtPlotRenderer : public QObject
{
    Q_OBJECT
public:
    enum DiscardFlag { DiscardBackground = 0x01, DiscardCanvasBackground = 0x02 };
    Q_DECLARE_FLAGS(DiscardFlags, DiscardFlag)
    enum LayoutFlag { DefaultLayout = 0x00, FrameWithScales = 0x01 };
    Q_DECLARE_FLAGS(LayoutFlags, LayoutFlag)

    explicit QwtPlotRenderer(QObject *parent = nullptr) : QObject(parent) {}
    ~QwtPlotRenderer() override = default;

    void setDiscardFlags(DiscardFlags flags) { m_discardFlags = flags; }
    void setLayoutFlags(LayoutFlags flags) { m_layoutFlags = flags; }

    void renderDocument(QwtPlot *plot, const QString &fileName, const QSizeF &sizeMM = QSizeF(200, 150), double dpi = 72) {
        Q_UNUSED(plot); Q_UNUSED(fileName); Q_UNUSED(sizeMM); Q_UNUSED(dpi);
    }

    void renderTo(QwtPlot *plot, QPrinter &printer) {
        Q_UNUSED(plot); Q_UNUSED(printer);
    }

    void render(QwtPlot *plot, QPainter *painter, const QRectF &rect) const {
        if (!plot || !painter) return;
        renderPlot(plot, painter, rect);
    }

private:
    void renderPlot(QwtPlot *plot, QPainter *painter, const QRectF &rect) const;
    void renderHistogram(QwtPlotHistogram *histogram, QPainter *painter,
                         const QwtScaleMap &xMap, const QwtScaleMap &yMap,
                         const QRectF &canvasRect) const;
    void renderCurve(QwtPlotCurve *curve, QPainter *painter,
                     const QwtScaleMap &xMap, const QwtScaleMap &yMap) const;

    DiscardFlags m_discardFlags;
    LayoutFlags m_layoutFlags;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(QwtPlotRenderer::DiscardFlags)
Q_DECLARE_OPERATORS_FOR_FLAGS(QwtPlotRenderer::LayoutFlags)
