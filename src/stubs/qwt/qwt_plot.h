#pragma once
// ANTIGRAVITY: stub header - QWT not installed
#include <QFrame>
#include "qwt_global.h"

class QwtText;
class QwtAbstractLegend;
class QwtScaleMap;
class QwtPlotCanvas;
class QwtPlotLayout;

class QWT_EXPORT QwtPlot : public QFrame
{
    Q_OBJECT
public:
    explicit QwtPlot(QWidget *parent = nullptr) : QFrame(parent) {}
    explicit QwtPlot(const QwtText &, QWidget *parent = nullptr) : QFrame(parent) {}
    ~QwtPlot() override {}

    void setTitle(const QString &) {}
    void setTitle(const QwtText &) {}
    void insertLegend(QwtAbstractLegend *, int = 1, double = -1) {}
    void setAxisTitle(int, const QString &) {}
    void setAxisTitle(int, const QwtText &) {}
    void setAxisScale(int, double, double, double = 0) {}
    void setAxisAutoScale(int, bool = true) {}
    void setAxisMaxMinor(int, int) {}
    void setAxisMaxMajor(int, int) {}
    void enableAxis(int, bool = true) {}
    void setCanvasBackground(const QBrush &) {}
    void replot() {}
    QwtPlotCanvas *canvas() { return nullptr; }
    QwtPlotLayout *plotLayout() { return nullptr; }

    enum Axis { yLeft = 0, yRight, xBottom, xTop, axisCnt };
};
