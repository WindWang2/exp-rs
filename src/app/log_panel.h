// src/app/log_panel.h — Log panel dock widget
#pragma once

#include <qgsdockwidget.h>
#include <qgsmessagelog.h>
#include <QString>

class QTextEdit;

class LogPanel : public QgsDockWidget
{
    Q_OBJECT
public:
    explicit LogPanel(QWidget *parent = nullptr);

    int messageCount() const;
    QString lastMessage() const;
    void clearMessages();

public slots:
    void logMessage(const QString &message, const QString &tag, Qgis::MessageLevel level);

private:
    QTextEdit *mTextEdit = nullptr;
    int mMessageCount = 0;
    QString mLastMessage;
};
