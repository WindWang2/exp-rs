#pragma once
// ANTIGRAVITY: stub
#include "qwt_global.h"
#include <QObject>
#include <QPen>
class QwtPlot;
class QWT_EXPORT QwtPlotItem : public QObject {
    Q_OBJECT
public:
    enum RttiValues { Rtti_PlotItem = 0, Rtti_PlotCurve = 1, Rtti_PlotHistogram = 6, Rtti_PlotMarker = 11 };
    QwtPlotItem() {}
    void attach(QwtPlot *) {}
    void detach() {}
    void setTitle(const QString &) {}
    void setZ(double) {}
    void setVisible(bool) {}
    virtual int rtti() const { return Rtti_PlotItem; }
};
class QWT_EXPORT QwtPlotCurve : public QwtPlotItem {
    Q_OBJECT
public:
    enum CurveStyle { NoCurve, Lines, Sticks, Steps, Dots };
    QwtPlotCurve() {}
    explicit QwtPlotCurve(const QString &) {}
    void setStyle(CurveStyle) {}
    void setPen(const QPen &, double = 0, int = 0) {}
    void setSamples(const QVector<double> &, const QVector<double> &) {}
    void setRenderHint(int, bool = true) {}
};
