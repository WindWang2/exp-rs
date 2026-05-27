#pragma once
// ANTIGRAVITY: QScintilla stub
#include <QObject>
#include <QColor>
#include <QFont>
#include <QString>
#include "qsciglobal.h"

class QsciScintilla;
class QsciAPIs;
class QSCINTILLA_EXPORT QsciLexer : public QObject
{
    Q_OBJECT
public:
    explicit QsciLexer(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~QsciLexer() {}
    virtual const char *language() const { return ""; }
    virtual const char *lexer() const { return ""; }
    virtual int lexerId() const { return 0; }
    virtual QString description(int) const { return {}; }
    virtual QColor defaultColor(int) const { return QColor(); }
    virtual QColor defaultPaper(int) const { return QColor(Qt::white); }
    virtual QFont defaultFont(int) const { return QFont(); }
    virtual const char *keywords(int) const { return nullptr; }
    virtual const char *wordCharacters() const { return "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"; }
    virtual bool caseSensitive() const { return true; }
    virtual void setAutoIndentStyle(int) {}
    virtual void setColor(const QColor &, int = -1) {}
    virtual void setFont(const QFont &, int = -1) {}
    virtual void setPaper(const QColor &, int = -1) {}
    virtual void setEditor(QsciScintilla *) {}
    virtual QsciAPIs *apis() const { return nullptr; }
    virtual void setAPIs(QsciAPIs *) {}
    virtual void refreshProperties() {}
    virtual int styleBitsNeeded() const { return 5; }
    virtual const char *autoCompletionFillups() const { return nullptr; }
    virtual QStringList autoCompletionWordSeparators() const { return {}; }
    virtual const char *blockEnd(int * = nullptr) const { return nullptr; }
    virtual const char *blockLookback(int * = nullptr) const { return nullptr; }
    virtual const char *blockStart(int * = nullptr) const { return nullptr; }
    virtual const char *blockStartKeyword(int * = nullptr) const { return nullptr; }
    virtual int braceStyle() const { return -1; }
    virtual int defaultStyle() const { return 0; }
    virtual bool eolFill(int) const { return false; }
    virtual bool defaultEolFill(int) const { return false; }
};
