#include "log_panel.h"
#include "widgets/rs_empty_state_widget.h"

#include <qgsapplication.h>
#include <QTextEdit>
#include <QStackedWidget>
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
    : QgsDockWidget(tr("系统日志"), parent)
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
    toolbarLayout->addWidget(new QLabel(tr("级别：")));
    m_levelFilter = new QComboBox(toolbar);
    m_levelFilter->addItem(tr("全部"), -1);
    m_levelFilter->addItem(tr("信息"), static_cast<int>(Qgis::MessageLevel::Info));
    m_levelFilter->addItem(tr("警告"), static_cast<int>(Qgis::MessageLevel::Warning));
    m_levelFilter->addItem(tr("错误"), static_cast<int>(Qgis::MessageLevel::Critical));
    m_levelFilter->addItem(tr("成功"), static_cast<int>(Qgis::MessageLevel::Success));
    m_levelFilter->setMinimumWidth(80);
    toolbarLayout->addWidget(m_levelFilter);

    // Tag filter
    toolbarLayout->addWidget(new QLabel(tr("标签：")));
    m_tagFilter = new QComboBox(toolbar);
    m_tagFilter->addItem(tr("全部"), "");
    m_tagFilter->setEditable(true);
    m_tagFilter->setMinimumWidth(100);
    toolbarLayout->addWidget(m_tagFilter);

    // Search
    toolbarLayout->addWidget(new QLabel(tr("搜索：")));
    m_searchEdit = new QLineEdit(toolbar);
    m_searchEdit->setPlaceholderText(tr("过滤日志消息..."));
    m_searchEdit->setClearButtonEnabled(true);
    toolbarLayout->addWidget(m_searchEdit);

    // Spacer
    toolbarLayout->addStretch();

    // Auto-scroll
    m_autoScrollCheck = new QCheckBox(tr("自动滚动"), toolbar);
    m_autoScrollCheck->setChecked(true);
    toolbarLayout->addWidget(m_autoScrollCheck);

    // Message count
    m_countLabel = new QLabel(tr("0 条消息"), toolbar);
    toolbarLayout->addWidget(m_countLabel);

    // Clear button
    m_clearButton = new QPushButton(tr("清空"), toolbar);
    toolbarLayout->addWidget(m_clearButton);

    mainLayout->addWidget(toolbar);

    // Text area stack with empty state
    m_textStack = new QStackedWidget(mainWidget);
    m_textStack->setObjectName(QStringLiteral("rsLogTextStack"));

    mTextEdit = new QTextEdit(m_textStack);
    mTextEdit->setReadOnly(true);
    mTextEdit->setLineWrapMode(QTextEdit::NoWrap);
    mTextEdit->document()->setMaximumBlockCount(50000);
    m_textStack->addWidget(mTextEdit); // Index 0: Text

    m_emptyState = new sicnu::RsEmptyStateWidget(
        QStringLiteral("chat_bubble_outline"),
        tr("暂无系统日志"),
        tr("系统运行正常，当前尚未产生日志消息。执行算法或操作时将在此显示详细记录。"),
        QString(),
        m_textStack);
    m_textStack->addWidget(m_emptyState); // Index 1: Empty
    m_textStack->setCurrentIndex(1); // Initially empty

    mainLayout->addWidget(m_textStack);

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

bool LogPanel::shouldShowMessage(const QString &message, const QString &tag, Qgis::MessageLevel level) const
{
    // Level filter
    int levelFilter = m_levelFilter->currentData().toInt();
    if (levelFilter != -1 && static_cast<int>(level) != levelFilter)
        return false;

    // Tag filter
    QString tagData = m_tagFilter->currentData().toString();
    QString tagText = m_tagFilter->currentText();
    if (!tagData.isEmpty() && tagText != tr("全部") && tagText != tr("All") && !tag.contains(tagText, Qt::CaseInsensitive))
        return false;

    // Search filter — match message body as well as tag
    QString searchText = m_searchEdit->text();
    if (!searchText.isEmpty()
        && !tag.contains(searchText, Qt::CaseInsensitive)
        && !message.contains(searchText, Qt::CaseInsensitive))
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
    m_countLabel->setText(tr("0 条消息"));
    if (m_textStack)
        m_textStack->setCurrentIndex(1);
}

void LogPanel::logMessage(const QString &message, const QString &tag, Qgis::MessageLevel level)
{
    int count = ++mMessageCount;
    mLastMessage = message;
    m_countLabel->setText(tr("%1 条消息").arg(count));

    // Add tag to filter dropdown if not already present
    if (m_tagFilter->findText(tag) == -1) {
        m_tagFilter->addItem(tag, tag);
    }

    if (!shouldShowMessage(message, tag, level))
        return;

    if (m_textStack)
        m_textStack->setCurrentIndex(0);

    QString prefix;
    QColor color;
    switch (level) {
        case Qgis::MessageLevel::Info:
            prefix = QStringLiteral("[INFO]");
            color = QColor("#0284c7"); // Sky blue
            break;
        case Qgis::MessageLevel::Warning:
            prefix = QStringLiteral("[WARN]");
            color = QColor("#d97706"); // Amber
            break;
        case Qgis::MessageLevel::Critical:
            prefix = QStringLiteral("[ERROR]");
            color = QColor("#dc2626"); // Red
            break;
        case Qgis::MessageLevel::Success:
            prefix = QStringLiteral("[OK]");
            color = QColor("#16a34a"); // Green
            break;
        case Qgis::MessageLevel::NoLevel:
            prefix = QStringLiteral("[LOG]");
            color = QColor("#64748b"); // Slate
            break;
    }

    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
    QString html = QStringLiteral("<span style='color:#888'>[%1]</span> "
                                  "<span style='color:%2;font-weight:bold'>%3</span> "
                                  "<span style='color:#aaa'>%4:</span> "
                                  "<span style='color:%5'>%6</span>")
                       .arg(timestamp, color.name(), prefix, tag, color.name(), message.toHtmlEscaped());

    if (!isVisible()) {
        if (m_pendingHtml.size() >= 1000)
            m_pendingHtml.removeFirst();
        m_pendingHtml.append(html);
        return;
    }

    mTextEdit->append(html);

    if (mAutoScroll) {
        QScrollBar *scrollBar = mTextEdit->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
    }
}

void LogPanel::showEvent(QShowEvent *event)
{
    QgsDockWidget::showEvent(event);
    flushPendingMessages();
}

void LogPanel::flushPendingMessages()
{
    if (m_pendingHtml.isEmpty())
        return;

    for (const QString &html : m_pendingHtml) {
        mTextEdit->append(html);
    }
    m_pendingHtml.clear();

    if (mAutoScroll) {
        QScrollBar *scrollBar = mTextEdit->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
    }
}
