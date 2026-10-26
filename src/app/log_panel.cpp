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
#include <QHash>
#include <QScrollBar>
#include <QAction>

namespace
{
// One message exactly as it arrived. The text area only holds what passed the
// filter at the time, so the panel retains the raw records to re-evaluate them
// whenever a filter control changes.
struct LogRecord
{
    QString message;
    QString tag;
    Qgis::MessageLevel level = Qgis::MessageLevel::NoLevel;
    QString html; // pre-rendered, so replaying keeps the original timestamp
};

// Everything retained for one panel.
struct RetainedLog
{
    QVector<LogRecord> records;
    int visible = 0; // records passing the current filters — what the label reports
};

// Mirrors mTextEdit->document()->setMaximumBlockCount(): retained history must
// not outgrow what the text area is allowed to display.
constexpr int kMaxRetainedMessages = 50000;

// logMessage() only ever runs on the GUI thread (QgsMessageLog delivers through
// a queued connection to this widget), so a plain per-panel map is enough.
QHash<const LogPanel *, RetainedLog> &retainedMessages()
{
    static QHash<const LogPanel *, RetainedLog> messages;
    return messages;
}
} // namespace

LogPanel::LogPanel(QWidget *parent)
    : QgsDockWidget(tr("System Log"), parent)
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
    m_levelFilter->addItem(tr("Succeeded"), static_cast<int>(Qgis::MessageLevel::Success));
    m_levelFilter->setMinimumWidth(80);
    toolbarLayout->addWidget(m_levelFilter);

    // Tag filter
    toolbarLayout->addWidget(new QLabel(tr("Tags:")));
    m_tagFilter = new QComboBox(toolbar);
    m_tagFilter->addItem(tr("All"), "");
    m_tagFilter->setEditable(true);
    m_tagFilter->setMinimumWidth(100);
    toolbarLayout->addWidget(m_tagFilter);

    // Search
    toolbarLayout->addWidget(new QLabel(tr("Search:")));
    m_searchEdit = new QLineEdit(toolbar);
    m_searchEdit->setPlaceholderText(tr("Filter log messages..."));
    m_searchEdit->setClearButtonEnabled(true);
    toolbarLayout->addWidget(m_searchEdit);

    // Spacer
    toolbarLayout->addStretch();

    // Auto-scroll
    m_autoScrollCheck = new QCheckBox(tr("Auto-Scroll"), toolbar);
    m_autoScrollCheck->setChecked(true);
    toolbarLayout->addWidget(m_autoScrollCheck);

    // Message count
    m_countLabel = new QLabel(tr("0 messages"), toolbar);
    toolbarLayout->addWidget(m_countLabel);

    // Clear button
    m_clearButton = new QPushButton(tr("Clear"), toolbar);
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
        tr("No system log yet"),
        tr("The system is running normally and no log messages have been produced yet. Detailed records appear here as algorithms and operations run."),
        QString(),
        m_textStack);
    m_textStack->addWidget(m_emptyState); // Index 1: Empty
    m_textStack->setCurrentIndex(1); // Initially empty

    mainLayout->addWidget(m_textStack);

    setWidget(mainWidget);

    // Connections
    connect(m_clearButton, &QPushButton::clicked, this, &LogPanel::clearMessages);

    // Retained history is keyed by panel; drop it once the panel is destroyed.
    connect(this, &QObject::destroyed, [](QObject *obj) {
        retainedMessages().remove(static_cast<const LogPanel *>(obj));
    });

    // Re-apply the current filters to everything already logged. The controls
    // used to be wired to nothing, so they only affected messages that arrived
    // afterwards; replaying the retained records makes them behave as labelled.
    const auto refilter = [this]() {
        RetainedLog &retained = retainedMessages()[this];

        mTextEdit->clear();
        m_pendingHtml.clear();

        int visible = 0;
        for (const LogRecord &record : retained.records)
        {
            if (!shouldShowMessage(record.message, record.tag, record.level))
                continue;

            ++visible;
            if (isVisible())
            {
                mTextEdit->append(record.html);
            }
            else
            {
                if (m_pendingHtml.size() >= 1000)
                    m_pendingHtml.removeFirst();
                m_pendingHtml.append(record.html);
            }
        }

        retained.visible = visible;
        m_countLabel->setText(tr("%1 messages").arg(visible));
        if (m_textStack)
            m_textStack->setCurrentIndex(visible > 0 ? 0 : 1);

        if (mAutoScroll)
        {
            QScrollBar *scrollBar = mTextEdit->verticalScrollBar();
            scrollBar->setValue(scrollBar->maximum());
        }
    };

    connect(m_levelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refilter);
    connect(m_tagFilter, QOverload<const QString &>::of(&QComboBox::currentTextChanged), this, refilter);
    connect(m_searchEdit, &QLineEdit::textChanged, this, refilter);

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

    // Tag filter. The combo is editable: while the "All" item is current the
    // user may still type a partial tag, and currentData() is empty in that
    // state — key the filter off the visible text compared against the "All"
    // item itself, or the typed filter is silently ignored.
    const QString allText = m_tagFilter->itemText(0);
    const bool tagFilterActive =
        m_tagFilter->currentIndex() > 0 || m_tagFilter->currentText() != allText;
    if (tagFilterActive
        && !tag.contains(m_tagFilter->currentText(), Qt::CaseInsensitive))
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
    RetainedLog &retained = retainedMessages()[this];
    retained.records.clear();
    retained.visible = 0;

    mTextEdit->clear();
    mMessageCount = 0;
    mLastMessage.clear();
    m_countLabel->setText(tr("0 messages"));
    if (m_textStack)
        m_textStack->setCurrentIndex(1);
}

void LogPanel::logMessage(const QString &message, const QString &tag, Qgis::MessageLevel level)
{
    ++mMessageCount;
    mLastMessage = message;

    // Add tag to filter dropdown if not already present
    if (m_tagFilter->findText(tag) == -1) {
        m_tagFilter->addItem(tag, tag);
    }

    // Retain the message before filtering it: a message the current filters drop
    // still has to be replayed when the user relaxes them.
    RetainedLog &retained = retainedMessages()[this];
    if (retained.records.size() >= kMaxRetainedMessages)
    {
        // Retire the oldest record, keeping the visible tally in step with what
        // the text area is still allowed to show.
        const LogRecord &dropped = retained.records.constFirst();
        const bool wasVisible = shouldShowMessage(dropped.message, dropped.tag, dropped.level);
        retained.records.removeFirst();
        if (wasVisible)
            --retained.visible;
    }

    LogRecord record;
    record.message = message;
    record.tag = tag;
    record.level = level;

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
    record.html = QStringLiteral("<span style='color:#888'>[%1]</span> "
                                 "<span style='color:%2;font-weight:bold'>%3</span> "
                                 "<span style='color:#aaa'>%4:</span> "
                                 "<span style='color:%5'>%6</span>")
                      .arg(timestamp, color.name(), prefix, tag, color.name(), message.toHtmlEscaped());

    retained.records.append(record);

    if (!shouldShowMessage(message, tag, level))
        return;

    if (m_textStack)
        m_textStack->setCurrentIndex(0);

    // The label reports what the filters show, so it cannot drift away from the
    // text area while part of the history is filtered out.
    ++retained.visible;
    m_countLabel->setText(tr("%1 messages").arg(retained.visible));

    if (!isVisible()) {
        if (m_pendingHtml.size() >= 1000)
            m_pendingHtml.removeFirst();
        m_pendingHtml.append(record.html);
        return;
    }

    mTextEdit->append(record.html);

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
