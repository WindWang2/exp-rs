#pragma once
// ANTIGRAVITY: QScintilla stub
#include "qsciscintilla.h"
#include <QStringList>

class QSCINTILLA_EXPORT QsciAPIs : public QObject
{
    Q_OBJECT
public:
    explicit QsciAPIs(QsciLexer *lexer = nullptr) : QObject(lexer) {}
    virtual ~QsciAPIs() {}

    void add(const QString &) {}
    void clear() {}
    bool load(const QString &) { return false; }
    void prepare() {}
    bool isPrepared() const { return false; }
    void cancelPrepare() {}
    void savePrepared(const QString &) const {}
    bool loadPrepared(const QString &) { return false; }
    void setAutoCompletionSource(QsciScintilla::AutoCompletionSource) {}
    QsciLexer *lexer() const { return nullptr; }

    // Virtual methods overridden by QgsSciApisExpression etc.
    virtual QStringList callTips(const QStringList &, int, QsciScintilla::CallTipsStyle, QList<int> &) { return {}; }
    virtual void updateAutoCompletionList(const QStringList &, QStringList &) {}
    virtual void autoCompletionSelected(const QString &) {}

Q_SIGNALS:
    void apiPreparationStarted();
    void apiPreparationCancelled();
    void apiPreparationFinished();
};
