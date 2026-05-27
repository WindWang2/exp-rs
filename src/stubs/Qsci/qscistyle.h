#pragma once
// ANTIGRAVITY: QScintilla stub
#include "qsciglobal.h"
#include <QColor>
#include <QFont>

class QSCINTILLA_EXPORT QsciStyle
{
public:
    QsciStyle(int style = -1) : mStyle(style) {}
    int style() const { return mStyle; }
    void setColor(const QColor &) {}
    void setPaper(const QColor &) {}
    void setFont(const QFont &) {}
    void apply(QsciScintillaBase *) const {}
private:
    int mStyle;
};
