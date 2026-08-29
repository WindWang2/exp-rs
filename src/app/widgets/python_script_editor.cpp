// python_script_editor.cpp — Dockable Python script editor implementation
#include "python_script_editor.h"
#include "python/qgis_python.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QGuiApplication>
#include <QPalette>
#include <QScrollBar>
#include <QKeySequence>
#include <QFont>
#include <QDebug>

namespace Sicnu {

void PythonScriptRunner::runScript(const QString &script)
{
    if (!QgisPython::instance().isInitialized()) {
        // Initialize from the worker thread is safe because QgisPython uses
        // a mutex internally.  However, typical usage initializes from the
        // main thread before running scripts.
        QgisPython::instance().initialize();
    }

    QString error;
    const bool success = QgisPython::instance().runString(script, error);
    emit finished(success, error);
}

PythonScriptEditor::PythonScriptEditor(QWidget *parent)
    : QWidget(parent)
    , m_runner(new PythonScriptRunner())
    , m_runnerThread(new QThread(this))
{
    setupUi();

    m_runner->moveToThread(m_runnerThread);
    connect(m_runner, &PythonScriptRunner::finished,
            this, &PythonScriptEditor::onExecutionFinished);
    connect(&QgisPython::instance(), &QgisPython::outputReady,
            this, &PythonScriptEditor::onPythonOutput,
            Qt::QueuedConnection);

    m_runnerThread->start(QThread::LowPriority);
}

PythonScriptEditor::~PythonScriptEditor()
{
    if (m_runnerThread) {
        m_runnerThread->requestInterruption();
        m_runnerThread->quit();
        if (!m_runnerThread->wait(1000)) {
            // Decouple to avoid QThread destructor fatal when a long-running script is still executing
            m_runnerThread->setParent(nullptr);
            QObject::connect(m_runnerThread, &QThread::finished, m_runnerThread, &QObject::deleteLater);
            if (m_runner) {
                // #650: the worker thread may still be inside the runner's
                // slot (that is exactly why the wait timed out) -
                // moveToThread() on an object executing in another thread is
                // undefined behavior. The runner is unparented, so retire it
                // on its own thread via the finished signal instead.
                QObject::connect(m_runnerThread, &QThread::finished, m_runner, &QObject::deleteLater);
                m_runner = nullptr;
            }
            m_runnerThread = nullptr;
        } else {
            delete m_runner;
            m_runner = nullptr;
        }
    }
}

void PythonScriptEditor::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Toolbar
    auto *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(4);

    m_runButton = new QPushButton(tr("Run"), this);
    m_runButton->setToolTip(tr("Run script (Ctrl+Enter)"));
    m_runButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));

    m_openButton = new QPushButton(tr("Open"), this);
    m_openButton->setToolTip(tr("Open Python script"));

    m_saveButton = new QPushButton(tr("Save"), this);
    m_saveButton->setToolTip(tr("Save Python script"));

    m_clearButton = new QPushButton(tr("Clear"), this);
    m_clearButton->setToolTip(tr("Clear output panel"));

    m_statusLabel = new QLabel(tr("Ready"), this);
    m_statusLabel->setEnabled(false);

    toolbarLayout->addWidget(m_runButton);
    toolbarLayout->addWidget(m_openButton);
    toolbarLayout->addWidget(m_saveButton);
    toolbarLayout->addWidget(m_clearButton);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_statusLabel);

    mainLayout->addLayout(toolbarLayout);

    // Script editor
    m_editor = new QPlainTextEdit(this);
    m_editor->setMinimumHeight(120);
    QFont editorFont(QStringLiteral("Monospace"));
    editorFont.setStyleHint(QFont::Monospace);
    editorFont.setPointSize(10);
    m_editor->setFont(editorFont);
    m_editor->setPlaceholderText(tr("Enter Python code here..."));
    mainLayout->addWidget(m_editor, 3);

    // Output panel
    m_output = new QTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setMinimumHeight(80);
    QFont outputFont(QStringLiteral("Monospace"));
    outputFont.setStyleHint(QFont::Monospace);
    outputFont.setPointSize(10);
    m_output->setFont(outputFont);
    m_output->setPlaceholderText(tr("Script output appears here..."));
    mainLayout->addWidget(m_output, 1);

    connect(m_runButton, &QPushButton::clicked, this, &PythonScriptEditor::runScript);
    connect(m_openButton, &QPushButton::clicked, this, &PythonScriptEditor::openScript);
    connect(m_saveButton, &QPushButton::clicked, this, &PythonScriptEditor::saveScript);
    connect(m_clearButton, &QPushButton::clicked, this, &PythonScriptEditor::clearOutput);
}

QString PythonScriptEditor::script() const
{
    return m_editor ? m_editor->toPlainText() : QString();
}

void PythonScriptEditor::setScript(const QString &script)
{
    if (m_editor)
        m_editor->setPlainText(script);
}

void PythonScriptEditor::appendOutput(const QString &text, bool isError)
{
    if (isError)
        writeError(text);
    else
        writeOutput(text);
}

void PythonScriptEditor::runScript()
{
    if (m_busy)
        return;

    const QString code = script().trimmed();
    if (code.isEmpty()) {
        emit statusMessage(tr("Script is empty"));
        return;
    }

    if (!QgisPython::instance().isInitialized()) {
        emit statusMessage(tr("Initializing Python..."));
        QgisPython::instance().initialize();
        if (!QgisPython::instance().isInitialized()) {
            writeError(tr("Failed to initialize Python interpreter.\n"));
            emit statusMessage(tr("Python initialization failed"));
            emit scriptExecuted(false);
            return;
        }
    }

    m_busy = true;
    m_runButton->setEnabled(false);
    m_statusLabel->setText(tr("Running..."));
    m_statusLabel->setEnabled(true);

    // Show the script being executed for clarity.
    writeOutput(QStringLiteral(">>> Running script...\n"));

    QMetaObject::invokeMethod(m_runner, "runScript", Qt::QueuedConnection,
                              Q_ARG(QString, code));
}

void PythonScriptEditor::openScript()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, tr("Open Python Script"), QString(),
        tr("Python scripts (*.py);;All files (*.*)"));

    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Open Script"),
                             tr("Cannot open file:\n%1").arg(fileName));
        return;
    }

    QTextStream stream(&file);
    setScript(stream.readAll());
    m_currentFile = fileName;
    emit statusMessage(tr("Opened %1").arg(fileName));
}

void PythonScriptEditor::saveScript()
{
    QString fileName = m_currentFile;
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getSaveFileName(
            this, tr("Save Python Script"), QString(),
            tr("Python scripts (*.py);;All files (*.*)"));
    }

    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Script"),
                             tr("Cannot write file:\n%1").arg(fileName));
        return;
    }

    QTextStream stream(&file);
    stream << script();
    m_currentFile = fileName;
    emit statusMessage(tr("Saved %1").arg(fileName));
}

void PythonScriptEditor::clearOutput()
{
    if (m_output)
        m_output->clear();
}

void PythonScriptEditor::onPythonOutput(const QString &text)
{
    writeOutput(text);
}

void PythonScriptEditor::onExecutionFinished(bool success, const QString &error)
{
    m_busy = false;
    m_runButton->setEnabled(true);

    if (!success && !error.isEmpty()) {
        writeError(error + QStringLiteral("\n"));
        m_statusLabel->setText(tr("Error"));
        emit statusMessage(tr("Script execution failed"));
    } else {
        m_statusLabel->setText(tr("Finished"));
        emit statusMessage(tr("Script finished"));
    }

    emit scriptExecuted(success);
}

void PythonScriptEditor::writeOutput(const QString &text)
{
    if (!m_output)
        return;

    m_output->setTextColor(QGuiApplication::palette().color(QPalette::Text));
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(text);

    QScrollBar *sb = m_output->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void PythonScriptEditor::writeError(const QString &text)
{
    if (!m_output)
        return;

    m_output->setTextColor(Qt::red);
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(text);
    m_output->setTextColor(QGuiApplication::palette().color(QPalette::Text));

    QScrollBar *sb = m_output->verticalScrollBar();
    sb->setValue(sb->maximum());
}

} // namespace Sicnu
