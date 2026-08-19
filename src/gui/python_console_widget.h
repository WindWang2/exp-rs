// src/gui/python_console_widget.h — LEGACY (VPATCH-7): not compiled, synchronous console.
// Live console is src/python/sicnu_python_console.cpp. Retained for reference only.
#pragma once

#include <QWidget>

class QPlainTextEdit;
class QPushButton;
class QTextEdit;

class PythonConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PythonConsoleWidget(QWidget *parent = nullptr);

public Q_SLOTS:
    void executeCommand();
    void clearOutput();

private:
    QPlainTextEdit *m_codeEditor = nullptr;
    QTextEdit *m_outputArea = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_clearButton = nullptr;

    void appendOutput(const QString &text, bool isError = false);
};
