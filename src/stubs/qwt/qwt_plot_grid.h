#pragma once
// ANTIGRAVITY: stub
#include "qwt_plot_curve.h"
class QWT_EXPORT QwtPlotGrid : public QwtPlotItem {
    Q_OBJECT
public:
    QwtPlotGrid() {}
    void enableX(bool) {}
    void enableY(bool) {}
    void setPen(const QPen &) {}
    void setMajorPen(const QPen &) {}
    void setMinorPen(const QPen &) {}
};
