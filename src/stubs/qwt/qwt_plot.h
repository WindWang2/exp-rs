#pragma once
// QwtPlot - minimal implementation with working canvas for histogram rendering
#include <QFrame>
#include <QPainter>
#include <QVBoxLayout>
#include "qwt_global.h"
#include "qwt_plot_canvas.h"

class QwtText;
class QwtAbstractLegend;
class QwtScaleMap;
class QwtPlotLayout;

class QWT_EXPORT QwtPlot : public QFrame
{
    Q_OBJECT
public:
    explicit QwtPlot(QWidget *parent = nullptr) : QFrame(parent) {
        m_canvas = new QwtPlotCanvas(this);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_canvas);
    }
    explicit QwtPlot(const QwtText &, QWidget *parent = nullptr) : QwtPlot(parent) {}
    ~QwtPlot() override = default;

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
    void setCanvasBackground(const QBrush &brush) { m_canvas->setBackgroundRole(QPalette::Window); }
    void replot() { if (m_canvas) m_canvas->update(); }
    QwtPlotCanvas *canvas() { return m_canvas; }
    QwtPlotLayout *plotLayout() { return nullptr; }

    enum Axis { yLeft = 0, yRight, xBottom, xTop, axisCnt };

private:
    QwtPlotCanvas *m_canvas = nullptr;
};
