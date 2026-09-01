// src/app/log_panel.h — Enhanced log panel with filtering and styling
#pragma once

#include <qgsdockwidget.h>
#include <qgsmessagelog.h>
#include <QString>
#include <atomic>

class QTextEdit;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;
class QStackedWidget;

namespace sicnu
{
class RsEmptyStateWidget;
}

/**
 * Enhanced log panel with:
 * - Color-coded log levels (info/warn/error/success)
 * - Module tag filtering
 * - Text search
 * - Auto-scroll toggle
 * - Message count display
 * - Level filtering
 */
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

protected:
    void showEvent(QShowEvent *event) override;

private:
    void setupUi();
    bool shouldShowMessage(const QString &message, const QString &tag, Qgis::MessageLevel level) const;
    void flushPendingMessages();

    QTextEdit *mTextEdit = nullptr;
    QStackedWidget *m_textStack = nullptr;
    sicnu::RsEmptyStateWidget *m_emptyState = nullptr;
    QComboBox *m_levelFilter = nullptr;
    QComboBox *m_tagFilter = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_clearButton = nullptr;
    QCheckBox *m_autoScrollCheck = nullptr;
    QLabel *m_countLabel = nullptr;

    std::atomic<int> mMessageCount{0};
    QString mLastMessage; // Only accessed from GUI thread via Qt::AutoConnection
    bool mAutoScroll = true;
    QVector<QString> m_pendingHtml;
};
