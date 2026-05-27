#pragma once
// ANTIGRAVITY: stub
#include <QFrame>
#include "qwt_global.h"
class QWT_EXPORT QwtPlotCanvas : public QFrame {
    Q_OBJECT
public:
    explicit QwtPlotCanvas(QwtPlot *p = nullptr) : QFrame(nullptr) { (void)p; }
};
