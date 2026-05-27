#pragma once
// ANTIGRAVITY: stub
#include "qwt_global.h"
#include <QList>
class QWT_EXPORT QwtScaleDiv {
public:
    enum TickType { MinorTick, MediumTick, MajorTick, NTickTypes };
    QwtScaleDiv() : mLower(0), mUpper(0) {}
    QwtScaleDiv(double l, double u) : mLower(l), mUpper(u) {}
    double lowerBound() const { return mLower; }
    double upperBound() const { return mUpper; }
    QList<double> ticks(int) const { return {}; }
private:
    double mLower, mUpper;
};
