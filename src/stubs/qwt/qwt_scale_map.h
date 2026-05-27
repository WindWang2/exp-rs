#pragma once
// ANTIGRAVITY: stub
#include "qwt_global.h"
class QWT_EXPORT QwtScaleMap {
public:
    double transform(double v) const { return v; }
    double invTransform(double v) const { return v; }
    double p1() const { return 0; }
    double p2() const { return 0; }
    double s1() const { return 0; }
    double s2() const { return 0; }
};
