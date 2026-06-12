#pragma once
// QwtPlotCurve - minimal implementation with data storage
#include "qwt_global.h"
#include <QObject>
#include <QPen>
#include <QVector>

class QwtPlot;

class QWT_EXPORT QwtPlotItem : public QObject {
    Q_OBJECT
public:
    enum RttiValues { Rtti_PlotItem = 0, Rtti_PlotCurve = 1, Rtti_PlotHistogram = 6, Rtti_PlotMarker = 11 };

    QwtPlotItem() = default;
    ~QwtPlotItem() override = default;

    void attach(QwtPlot *) {}
    void detach() {}
    void setTitle(const QString &title) { m_title = title; }
    void setZ(double z) { m_z = z; }
    void setVisible(bool visible) { m_visible = visible; }
    virtual int rtti() const { return Rtti_PlotItem; }

    const QString &title() const { return m_title; }
    double z() const { return m_z; }
    bool isVisible() const { return m_visible; }

private:
    QString m_title;
    double m_z = 0.0;
    bool m_visible = true;
};

class QWT_EXPORT QwtPlotCurve : public QwtPlotItem {
    Q_OBJECT
public:
    enum CurveStyle { NoCurve, Lines, Sticks, Steps, Dots };

    QwtPlotCurve() = default;
    explicit QwtPlotCurve(const QString &title) { setTitle(title); }
    ~QwtPlotCurve() override = default;

    void setStyle(CurveStyle style) { m_style = style; }
    void setPen(const QPen &pen, double = 0, int = 0) { m_pen = pen; }
    void setSamples(const QVector<double> &x, const QVector<double> &y) { m_xData = x; m_yData = y; }
    void setRenderHint(int, bool = true) {}

    int rtti() const override { return Rtti_PlotCurve; }

    const QVector<double> &xData() const { return m_xData; }
    const QVector<double> &yData() const { return m_yData; }
    CurveStyle style() const { return m_style; }
    const QPen &pen() const { return m_pen; }

private:
    QVector<double> m_xData;
    QVector<double> m_yData;
    CurveStyle m_style = Lines;
    QPen m_pen;
};
