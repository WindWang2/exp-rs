#pragma once
// ANTIGRAVITY: QScintilla stub
#include <QAbstractScrollArea>
#include "qsciglobal.h"

class QSCINTILLA_EXPORT QsciScintillaBase : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit QsciScintillaBase(QWidget *parent = nullptr) : QAbstractScrollArea(parent) {}
    virtual ~QsciScintillaBase();

    // Constants used by QgsCodeEditor
    enum {
        INDIC_MAX = 35,
        INDICATOR_MAX = INDIC_MAX,
        MARKNUM_HISTORY_SAVED = 0,
        MARKNUM_HISTORY_CURRENT = 1,
        MARKNUM_HISTORY_REVERTED = 2,
        MARKNUM_HISTORY_REVERTED_ORIGIN = 3,
    };
    static const int MAXSCI = INDIC_MAX;

    long sendScintilla(unsigned int, ...) { return 0; }
    long sendScintilla(unsigned int, long, ...) { return 0; }
    long sendScintilla(unsigned int, const char *) { return 0; }
};
