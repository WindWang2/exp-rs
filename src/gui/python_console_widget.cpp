// src/gui/python_console_widget.cpp — LEGACY (VPATCH-7): not compiled.
#include "python_console_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QKeySequence>

#include "python/qgis_python.h"

PythonConsoleWidget::PythonConsoleWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Toolbar
    auto *toolbarLayout = new QHBoxLayout();
    m_runButton = new QPushButton(tr("Run"), this);
    m_runButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    m_clearButton = new QPushButton(tr("Clear"), this);
    toolbarLayout->addWidget(m_runButton);
    toolbarLayout->addWidget(m_clearButton);
    toolbarLayout->addStretch();
    mainLayout->addLayout(toolbarLayout);

    // Code editor (input) — plain text edit with monospace font
    m_codeEditor = new QPlainTextEdit(this);
    m_codeEditor->setMinimumHeight(100);
    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    m_codeEditor->setFont(monoFont);
    m_codeEditor->setPlaceholderText(tr("Enter Python code here..."));
    mainLayout->addWidget(m_codeEditor, 2);

    // Output area
    m_outputArea = new QTextEdit(this);
    m_outputArea->setReadOnly(true);
    m_outputArea->setMinimumHeight(80);
    QFont outputFont(QStringLiteral("Monospace"));
    outputFont.setStyleHint(QFont::Monospace);
    outputFont.setPointSize(10);
    m_outputArea->setFont(outputFont);
    mainLayout->addWidget(m_outputArea, 1);

    // Connections (function-pointer syntax — no SIGNAL/SLOT macros needed)
    connect(m_runButton, &QPushButton::clicked, this, &PythonConsoleWidget::executeCommand);
    connect(m_clearButton, &QPushButton::clicked, this, &PythonConsoleWidget::clearOutput);
}

void PythonConsoleWidget::executeCommand()
{
    QString code = m_codeEditor->toPlainText();
    if (code.trimmed().isEmpty())
        return;

    appendOutput(QStringLiteral(">>> %1").arg(code));

    // Initialize Python if needed
    if (!QgisPython::instance().isInitialized())
        QgisPython::instance().initialize();

    QString error;
    bool success = QgisPython::instance().runString(code, error);

    if (!error.isEmpty())
        appendOutput(error, true);
    else if (!success)
        appendOutput(tr("Execution failed"), true);
}

void PythonConsoleWidget::clearOutput()
{
    m_outputArea->clear();
}

void PythonConsoleWidget::appendOutput(const QString &text, bool isError)
{
    if (isError)
        m_outputArea->setTextColor(Qt::red);
    else
        m_outputArea->setTextColor(QGuiApplication::palette().color(QPalette::Text));

    m_outputArea->append(text);

    // Auto-scroll to bottom
    QScrollBar *sb = m_outputArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}
