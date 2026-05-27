#pragma once
#include "qscilexer.h"
class QSCINTILLA_EXPORT QsciLexerCSS : public QsciLexer {
    Q_OBJECT
public:
    explicit QsciLexerCSS(QObject *p = nullptr) : QsciLexer(p) {}
    const char *language() const override { return "CSS"; }
    const char *lexer() const override { return "css"; }
};
