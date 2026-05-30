#pragma once
// ANTIGRAVITY: stub
#include <QObject>
#include "qwt_global.h"
#include "qwt_picker_machine.h"
class QwtPlotCanvas;
class QwtPlot;
class QWT_EXPORT QwtPicker : public QObject {
public:
    explicit QwtPicker(QWidget *parent = nullptr) : QObject(parent) {}
    void setStateMachine(QwtPickerMachine *) {}
    void setRubberBandPen(const QPen &) {}
    void setTrackerPen(const QPen &) {}
    virtual ~QwtPicker() {}
};
class QWT_EXPORT QwtPlotPicker : public QwtPicker {
public:
    explicit QwtPlotPicker(QWidget *canvas) : QwtPicker(canvas) {}
    QwtPlotPicker(int xAxis, int yAxis, QWidget *canvas) : QwtPicker(canvas) { (void)xAxis; (void)yAxis; }
    QwtPlotPicker(int xAxis, int yAxis, int rubberBand, int trackerMode, QWidget *canvas)
        : QwtPicker(canvas) { (void)xAxis; (void)yAxis; (void)rubberBand; (void)trackerMode; }
Q_SIGNALS:
    void selected(const QPointF &);
    void moved(const QPointF &);
    void appended(const QPointF &);
};
