#pragma once
// ANTIGRAVITY: QScintilla stub
#include "qscilexer.h"
class QSCINTILLA_EXPORT QsciLexerPython : public QsciLexer {
    Q_OBJECT
public:
    explicit QsciLexerPython(QObject *p = nullptr) : QsciLexer(p) {}
    const char *language() const override { return "Python"; }
    const char *lexer() const override { return "python"; }
    enum { Default = 0, Comment = 1, Number = 2, DoubleQuotedString = 3,
           SingleQuotedString = 4, Keyword = 5, TripleSingleQuotedString = 6,
           TripleDoubleQuotedString = 7, ClassName = 8, FunctionMethodName = 9,
           Operator = 10, Identifier = 11, CommentBlock = 12,
           UnclosedString = 13, HighlightedIdentifier = 14, Decorator = 15 };
    void setIndentationWarning(int) {}
    int indentationWarning() const { return 0; }
};
