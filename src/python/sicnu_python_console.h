// sicnu_python_console.h — Simple Python console widget
#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QStringList>

/**
 * A simple Python console widget for SICNU GEO RS.
 * Provides a REPL interface to the embedded Python interpreter.
 */
class SicnuPythonConsole : public QWidget
{
    Q_OBJECT

public:
    explicit SicnuPythonConsole(QWidget *parent = nullptr);

    /**
     * Execute a Python command and display the output.
     */
    void executeCommand(const QString &command);

    /**
     * Clear the console output.
     */
    void clearConsole();

    /**
     * Write text to the console output.
     */
    void writeOutput(const QString &text);

    /**
     * Write error text to the console output.
     */
    void writeError(const QString &text);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onCommandEntered();
    void onPythonOutput(const QString &text);

private:
    void setupUi();
    void showPrompt();
    void addHistory(const QString &command);

    QPlainTextEdit *m_outputWidget = nullptr;
    QLineEdit *m_inputLine = nullptr;

    QStringList m_history;
    int m_historyIndex = -1;
    bool m_busy = false;
};
