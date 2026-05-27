#pragma once
// ANTIGRAVITY: stub
#include "qwt_global.h"
#include <QBrush>
#include <QPen>
class QWT_EXPORT QwtSymbol {
public:
    enum Style { NoSymbol = -1, Ellipse, Rect, Diamond, Triangle, DTriangle, UTriangle, LTriangle, RTriangle, Cross, XCross, HLine, VLine, Star1, Star2, Hexagon, Path };
    explicit QwtSymbol(Style s = NoSymbol) : mStyle(s) {}
    void setStyle(Style s) { mStyle = s; }
    void setPen(const QPen &) {}
    void setBrush(const QBrush &) {}
    void setSize(int, int = -1) {}
private:
    Style mStyle;
};
