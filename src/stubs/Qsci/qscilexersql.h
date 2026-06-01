#pragma once
#include "qscilexer.h"
class QSCINTILLA_EXPORT QsciLexerSQL : public QsciLexer {
    Q_OBJECT
public:
    explicit QsciLexerSQL(QObject *p = nullptr) : QsciLexer(p) {}
    ~QsciLexerSQL() override;
    const char *language() const override { return "SQL"; }
    const char *lexer() const override { return "sql"; }
    enum { Default = 0, Comment = 1, CommentLine = 2, CommentDoc = 3,
           Number = 4, Keyword = 5, DoubleQuotedString = 6,
           SingleQuotedString = 7, PlusKeyword = 8, PlusPrompt = 9,
           Operator = 10, Identifier = 11 };
};
