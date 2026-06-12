#pragma once
// QwtPlotCanvas - minimal implementation for histogram rendering
#include <QFrame>
#include "qwt_global.h"

class QwtPlot;

class QWT_EXPORT QwtPlotCanvas : public QFrame
{
    Q_OBJECT
public:
    explicit QwtPlotCanvas(QwtPlot *p = nullptr) : QFrame(nullptr) { Q_UNUSED(p); }
    ~QwtPlotCanvas() override = default;
};
