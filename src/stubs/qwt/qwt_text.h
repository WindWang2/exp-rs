#pragma once
// ANTIGRAVITY: stub header - QWT not installed
#include <QString>
#include "qwt_global.h"

class QWT_EXPORT QwtText
{
public:
    QwtText() {}
    QwtText(const QString &s) : mText(s) {}
    QwtText(const char *s) : mText(QString::fromLatin1(s)) {}
    QString text() const { return mText; }
private:
    QString mText;
};
