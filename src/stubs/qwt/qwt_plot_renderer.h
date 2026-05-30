#pragma once
// ANTIGRAVITY: stub header - QWT not installed
#include <QObject>
#include <QPainter>
#include "qwt_global.h"
class QwtPlot;
class QPrinter;
class QWT_EXPORT QwtPlotRenderer : public QObject {
public:
    enum DiscardFlag { DiscardBackground = 0x01, DiscardCanvasBackground = 0x02 };
    Q_DECLARE_FLAGS(DiscardFlags, DiscardFlag)
    enum LayoutFlag { DefaultLayout = 0x00, FrameWithScales = 0x01 };
    Q_DECLARE_FLAGS(LayoutFlags, LayoutFlag)

    explicit QwtPlotRenderer(QObject *parent = nullptr) : QObject(parent) {}
    void setDiscardFlags(DiscardFlags) {}
    void setLayoutFlags(LayoutFlags) {}
    void renderDocument(QwtPlot *, const QString &, const QSizeF &, double = 72) {}
    void render(QwtPlot *, QPainter *, const QRectF &) {}
    void renderTo(QwtPlot *, QPrinter &) {}
};
Q_DECLARE_OPERATORS_FOR_FLAGS(QwtPlotRenderer::DiscardFlags)
Q_DECLARE_OPERATORS_FOR_FLAGS(QwtPlotRenderer::LayoutFlags)
