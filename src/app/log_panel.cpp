// src/app/log_panel.cpp — Enhanced log panel implementation
#include "log_panel.h"

#include <qgsapplication.h>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDateTime>
#include <QScrollBar>
#include <QAction>

LogPanel::LogPanel(QWidget *parent)
    : QgsDockWidget(tr("Log"), parent)
{
    setupUi();

    // Connect to QgsMessageLog
    connect(QgsApplication::messageLog(), &QgsMessageLog::messageReceivedWithFormat,
            this, [this](const QString &message, const QString &tag, Qgis::MessageLevel level, Qgis::StringFormat) {
                logMessage(message, tag, level);
            });
}

void LogPanel::setupUi()
{
    auto *mainWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Toolbar
    auto *toolbar = new QWidget(mainWidget);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(4);

    // Level filter
    toolbarLayout->addWidget(new QLabel(tr("Level:")));
    m_levelFilter = new QComboBox(toolbar);
    m_levelFilter->addItem(tr("All"), -1);
    m_levelFilter->addItem(tr("Info"), static_cast<int>(Qgis::MessageLevel::Info));
    m_levelFilter->addItem(tr("Warning"), static_cast<int>(Qgis::MessageLevel::Warning));
    m_levelFilter->addItem(tr("Error"), static_cast<int>(Qgis::MessageLevel::Critical));
    m_levelFilter->addItem(tr("Success"), static_cast<int>(Qgis::MessageLevel::Success));
    m_levelFilter->setMinimumWidth(80);
    toolbarLayout->addWidget(m_levelFilter);

    // Tag filter
    toolbarLayout->addWidget(new QLabel(tr("Tag:")));
    m_tagFilter = new QComboBox(toolbar);
    m_tagFilter->addItem(tr("All"), "");
    m_tagFilter->setEditable(true);
    m_tagFilter->setMinimumWidth(100);
    toolbarLayout->addWidget(m_tagFilter);

    // Search
    toolbarLayout->addWidget(new QLabel(tr("Search:")));
    m_searchEdit = new QLineEdit(toolbar);
    m_searchEdit->setPlaceholderText(tr("Filter messages..."));
    m_searchEdit->setClearButtonEnabled(true);
    toolbarLayout->addWidget(m_searchEdit);

    // Spacer
    toolbarLayout->addStretch();

    // Auto-scroll
    m_autoScrollCheck = new QCheckBox(tr("Auto-scroll"), toolbar);
    m_autoScrollCheck->setChecked(true);
    toolbarLayout->addWidget(m_autoScrollCheck);

    // Message count
    m_countLabel = new QLabel(tr("0 messages"), toolbar);
    toolbarLayout->addWidget(m_countLabel);

    // Clear button
    m_clearButton = new QPushButton(tr("Clear"), toolbar);
    toolbarLayout->addWidget(m_clearButton);

    mainLayout->addWidget(toolbar);

    // Text area
    mTextEdit = new QTextEdit(mainWidget);
    mTextEdit->setReadOnly(true);
    mTextEdit->setLineWrapMode(QTextEdit::NoWrap);
    mTextEdit->document()->setMaximumBlockCount(50000);
    mainLayout->addWidget(mTextEdit);

    setWidget(mainWidget);

    // Connections
    connect(m_clearButton, &QPushButton::clicked, this, &LogPanel::clearMessages);
    connect(m_levelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        // Re-filter would require storing all messages; for now just inform
    });
    connect(m_autoScrollCheck, &QCheckBox::toggled, this, [this](bool checked) {
        mAutoScroll = checked;
    });
}

bool LogPanel::shouldShowMessage(const QString &tag, Qgis::MessageLevel level) const
{
    // Level filter
    int levelFilter = m_levelFilter->currentData().toInt();
    if (levelFilter != -1 && static_cast<int>(level) != levelFilter)
        return false;

    // Tag filter
    QString tagFilter = m_tagFilter->currentText();
    if (!tagFilter.isEmpty() && tagFilter != tr("All") && !tag.contains(tagFilter, Qt::CaseInsensitive))
        return false;

    // Search filter
    QString searchText = m_searchEdit->text();
    if (!searchText.isEmpty() && !tag.contains(searchText, Qt::CaseInsensitive))
        return false;

    return true;
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
    m_countLabel->setText(tr("0 messages"));
}

void LogPanel::logMessage(const QString &message, const QString &tag, Qgis::MessageLevel level)
{
    int count = ++mMessageCount;
    mLastMessage = message;
    m_countLabel->setText(tr("%1 messages").arg(count));

    // Add tag to filter dropdown if not already present
    if (m_tagFilter->findText(tag) == -1) {
        m_tagFilter->addItem(tag, tag);
    }

    if (!shouldShowMessage(tag, level))
        return;

    QString prefix;
    QColor color;
    switch (level) {
        case Qgis::MessageLevel::Info:
            prefix = QStringLiteral("[INFO]");
            color = QColor(100, 180, 255); // Blue
            break;
        case Qgis::MessageLevel::Warning:
            prefix = QStringLiteral("[WARN]");
            color = QColor(255, 200, 50); // Yellow
            break;
        case Qgis::MessageLevel::Critical:
            prefix = QStringLiteral("[ERROR]");
            color = QColor(255, 100, 100); // Red
            break;
        case Qgis::MessageLevel::Success:
            prefix = QStringLiteral("[OK]");
            color = QColor(100, 220, 100); // Green
            break;
        case Qgis::MessageLevel::NoLevel:
            prefix = QStringLiteral("[LOG]");
            color = QColor(180, 180, 180); // Gray
            break;
    }

    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
    QString html = QStringLiteral("<span style='color:#888'>[%1]</span> "
                                  "<span style='color:%2;font-weight:bold'>%3</span> "
                                  "<span style='color:#aaa'>%4:</span> "
                                  "<span style='color:%5'>%6</span>")
                       .arg(timestamp, color.name(), prefix, tag, color.name(), message.toHtmlEscaped());

    mTextEdit->append(html);

    if (mAutoScroll) {
        QScrollBar *scrollBar = mTextEdit->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
    }
}
