#pragma once
// ANTIGRAVITY: stub
#include "qwt_plot_picker.h"
class QWT_EXPORT QwtPlotZoomer : public QwtPlotPicker {
    Q_OBJECT
public:
    explicit QwtPlotZoomer(QWidget *canvas) : QwtPlotPicker(canvas) {}
    QwtPlotZoomer(int xAxis, int yAxis, QWidget *canvas) : QwtPlotPicker(xAxis, yAxis, canvas) {}
    void setMaxStackDepth(int) {}
    void zoom(int) {}
Q_SIGNALS:
    void zoomed(const QRectF &);
};
