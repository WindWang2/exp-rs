#pragma once
// ANTIGRAVITY: stub
#include "qwt_plot_curve.h"
class QWT_EXPORT QwtPlotMarker : public QwtPlotItem {
public:
    QwtPlotMarker() {}
    void setXValue(double) {}
    void setYValue(double) {}
    void setValue(double, double) {}
    void setLineStyle(int) {}
    void setLinePen(const QPen &) {}
    void setLabel(const QwtText &) {}
    enum LineStyle { NoLine, HLine, VLine, Cross };
};
