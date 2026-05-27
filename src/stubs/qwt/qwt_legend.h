#pragma once
// ANTIGRAVITY: stub header - QWT not installed
#include <QFrame>
#include "qwt_global.h"

class QWT_EXPORT QwtAbstractLegend : public QFrame {
    Q_OBJECT
public:
    explicit QwtAbstractLegend(QWidget *p = nullptr) : QFrame(p) {}
};

class QWT_EXPORT QwtLegend : public QwtAbstractLegend {
    Q_OBJECT
public:
    explicit QwtLegend(QWidget *p = nullptr) : QwtAbstractLegend(p) {}
};
