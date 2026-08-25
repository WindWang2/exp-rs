#pragma once
#include "qscilexer.h"
class QSCINTILLA_EXPORT QsciLexerHTML : public QsciLexer {
    Q_OBJECT
public:
    explicit QsciLexerHTML(QObject *p = nullptr) : QsciLexer(p) {}
    ~QsciLexerHTML() override;
    const char *language() const override { return "HTML"; }
    const char *lexer() const override { return "hypertext"; }
};
