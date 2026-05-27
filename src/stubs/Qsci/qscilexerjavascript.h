#pragma once
#include "qscilexer.h"
class QSCINTILLA_EXPORT QsciLexerJavaScript : public QsciLexer {
    Q_OBJECT
public:
    explicit QsciLexerJavaScript(QObject *p = nullptr) : QsciLexer(p) {}
    const char *language() const override { return "JavaScript"; }
    const char *lexer() const override { return "cpp"; }
};
