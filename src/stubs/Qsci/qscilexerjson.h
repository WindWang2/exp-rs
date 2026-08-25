#pragma once
#include "qscilexer.h"
class QSCINTILLA_EXPORT QsciLexerJSON : public QsciLexer {
    Q_OBJECT
public:
    explicit QsciLexerJSON(QObject *p = nullptr) : QsciLexer(p) {}
    ~QsciLexerJSON() override;
    const char *language() const override { return "JSON"; }
    const char *lexer() const override { return "json"; }
    void setHighlightEscapeSequences(bool) {}
    bool highlightEscapeSequences() const { return false; }
    void setFoldCompact(bool) {}
    bool foldCompact() const { return false; }
};
