#pragma once
// ANTIGRAVITY: stub
#include "qwt_plot_curve.h"
#include <QBrush>
#include <QVector>
class QwtIntervalSample {
public:
    QwtIntervalSample() {}
    QwtIntervalSample(double v, double l, double u) : value(v), interval(l, u) {}
    double value = 0;
    struct { double minValue() const { return mn; } double maxValue() const { return mx; } double mn = 0, mx = 0; } interval;
    QwtIntervalSample(double v, QPair<double,double> p) : value(v) { interval.mn = p.first; interval.mx = p.second; }
};
class QWT_EXPORT QwtPlotHistogram : public QwtPlotItem {
public:
    QwtPlotHistogram() {}
    explicit QwtPlotHistogram(const QString &) {}
    void setBrush(const QBrush &) {}
    void setPen(const QPen &) {}
    void setSamples(const QVector<QwtIntervalSample> &) {}
    void setBaseline(double) {}
};
