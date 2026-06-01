// src/app/log_panel.cpp — Log panel implementation
#include "log_panel.h"

#include <qgsapplication.h>
#include <QTextEdit>
#include <QVBoxLayout>

LogPanel::LogPanel(QWidget *parent)
    : QgsDockWidget(tr("Log"), parent)
{
    mTextEdit = new QTextEdit(this);
    mTextEdit->setReadOnly(true);
    setWidget(mTextEdit);

    // Connect to QgsMessageLog
    connect(QgsApplication::messageLog(), &QgsMessageLog::messageReceivedWithFormat,
            this, [this](const QString &message, const QString &tag, Qgis::MessageLevel level, Qgis::StringFormat) {
                logMessage(message, tag, level);
            });
}

int LogPanel::messageCount() const
{
    return mMessageCount;
}

QString LogPanel::lastMessage() const
{
    return mLastMessage;
}

void LogPanel::clearMessages()
{
    mTextEdit->clear();
    mMessageCount = 0;
    mLastMessage.clear();
}

void LogPanel::logMessage(const QString &message, const QString &tag, Qgis::MessageLevel level)
{
    mMessageCount++;
    mLastMessage = message;

    QString prefix;
    switch (level) {
        case Qgis::MessageLevel::Info:     prefix = QStringLiteral("[INFO]"); break;
        case Qgis::MessageLevel::Warning:  prefix = QStringLiteral("[WARN]"); break;
        case Qgis::MessageLevel::Critical: prefix = QStringLiteral("[ERROR]"); break;
        case Qgis::MessageLevel::Success:  prefix = QStringLiteral("[OK]"); break;
        case Qgis::MessageLevel::NoLevel:  prefix = QStringLiteral("[LOG]"); break;
    }

    mTextEdit->append(QStringLiteral("%1 %2: %3").arg(prefix, tag, message));
}
