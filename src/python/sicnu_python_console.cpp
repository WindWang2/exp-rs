// sicnu_python_console.cpp — Simple Python console widget
#include "sicnu_python_console.h"
#include "qgis_python.h"

#include <QKeyEvent>
#include <QScrollBar>
#include <QFont>
#include <QApplication>
#include <QDateTime>

SicnuPythonConsole::SicnuPythonConsole(QWidget *parent)
    : QWidget(parent)
{
    setupUi();

    // Connect Python output to console
    connect(&QgisPython::instance(), &QgisPython::outputReady,
            this, &SicnuPythonConsole::onPythonOutput);

    // Initialize Python if not already done
    if (!QgisPython::instance().isInitialized()) {
        QgisPython::instance().initialize();
    }

    showPrompt();
}

void SicnuPythonConsole::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Output area
    m_outputWidget = new QPlainTextEdit(this);
    m_outputWidget->setReadOnly(true);
    m_outputWidget->setMaximumBlockCount(10000);
    m_outputWidget->setFont(QFont("Monospace", 10));
    m_outputWidget->setStyleSheet(
        "QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }");
    layout->addWidget(m_outputWidget);

    // Input line
    m_inputLine = new QLineEdit(this);
    m_inputLine->setFont(QFont("Monospace", 10));
    m_inputLine->setPlaceholderText(tr("Enter Python code..."));
    m_inputLine->setStyleSheet(
        "QLineEdit { background-color: #2d2d2d; color: #d4d4d4; border: 1px solid #404040; padding: 4px; }");
    m_inputLine->installEventFilter(this);
    layout->addWidget(m_inputLine);

    connect(m_inputLine, &QLineEdit::returnPressed, this, &SicnuPythonConsole::onCommandEntered);
}

bool SicnuPythonConsole::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_inputLine && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->key() == Qt::Key_Up) {
            // Navigate history up
            if (!m_history.isEmpty() && m_historyIndex > 0) {
                m_historyIndex--;
                m_inputLine->setText(m_history[m_historyIndex]);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            // Navigate history down
            if (m_historyIndex < m_history.size() - 1) {
                m_historyIndex++;
                m_inputLine->setText(m_history[m_historyIndex]);
            } else {
                m_historyIndex = m_history.size();
                m_inputLine->clear();
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void SicnuPythonConsole::executeCommand(const QString &command)
{
    if (command.trimmed().isEmpty())
        return;

    m_busy = true;
    m_inputLine->setEnabled(false);

    // Show command in output
    writeOutput(QString(">>> %1\n").arg(command));

    // Execute
    QString error;
    bool success = QgisPython::instance().runString(command, error);

    if (!success && !error.isEmpty()) {
        writeError(error + "\n");
    }

    m_busy = false;
    m_inputLine->setEnabled(true);
    showPrompt();
}

void SicnuPythonConsole::onCommandEntered()
{
    QString command = m_inputLine->text();
    m_inputLine->clear();

    if (!command.trimmed().isEmpty()) {
        addHistory(command);
        executeCommand(command);
    }
}

void SicnuPythonConsole::onPythonOutput(const QString &text)
{
    writeOutput(text);
}

void SicnuPythonConsole::clearConsole()
{
    m_outputWidget->clear();
    showPrompt();
}

void SicnuPythonConsole::writeOutput(const QString &text)
{
    m_outputWidget->moveCursor(QTextCursor::End);
    m_outputWidget->insertPlainText(text);
    m_outputWidget->verticalScrollBar()->setValue(m_outputWidget->verticalScrollBar()->maximum());
}

void SicnuPythonConsole::writeError(const QString &text)
{
    m_outputWidget->moveCursor(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(QColor("#ff6b6b"));
    QTextCursor cursor = m_outputWidget->textCursor();
    cursor.setCharFormat(format);
    m_outputWidget->setTextCursor(cursor);
    m_outputWidget->insertPlainText(text);
    format.setForeground(QColor("#d4d4d4"));
    cursor.setCharFormat(format);
    m_outputWidget->setTextCursor(cursor);
    m_outputWidget->verticalScrollBar()->setValue(m_outputWidget->verticalScrollBar()->maximum());
}

void SicnuPythonConsole::showPrompt()
{
    // Don't add another prompt if there's already one
}

void SicnuPythonConsole::addHistory(const QString &command)
{
    if (!command.trimmed().isEmpty()) {
        m_history.append(command);
        if (m_history.size() > 1000) {
            m_history.removeFirst();
        }
        m_historyIndex = m_history.size();
    }
}
