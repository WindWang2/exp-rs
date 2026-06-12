#pragma once
// QwtPlotHistogram - minimal implementation with data storage
#include "qwt_plot_curve.h"
#include <QBrush>
#include <QPen>
#include <QVector>

class QwtIntervalSample {
public:
    QwtIntervalSample() = default;
    QwtIntervalSample(double v, double l, double u) : value(v), m_mn(l), m_mx(u) {}
    QwtIntervalSample(double v, QPair<double,double> p) : value(v), m_mn(p.first), m_mx(p.second) {}

    double value = 0;

    struct Interval {
        double minValue() const { return mn; }
        double maxValue() const { return mx; }
        double mn = 0, mx = 0;
    };

    Interval interval;

private:
    double m_mn = 0, m_mx = 0;
};

class QWT_EXPORT QwtPlotHistogram : public QwtPlotItem {
public:
    QwtPlotHistogram() = default;
    explicit QwtPlotHistogram(const QString &) {}
    ~QwtPlotHistogram() override = default;

    void setBrush(const QBrush &brush) { m_brush = brush; }
    void setPen(const QPen &pen) { m_pen = pen; }
    void setSamples(const QVector<QwtIntervalSample> &samples) { m_samples = samples; }
    void setBaseline(double baseline) { m_baseline = baseline; }

    const QVector<QwtIntervalSample> &samples() const { return m_samples; }
    const QBrush &brush() const { return m_brush; }
    const QPen &pen() const { return m_pen; }
    double baseline() const { return m_baseline; }

private:
    QVector<QwtIntervalSample> m_samples;
    QBrush m_brush;
    QPen m_pen;
    double m_baseline = 0.0;
};
