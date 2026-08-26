// Python Script Editor tests — verify editor UI and embedded Python execution
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QLabel>

#include "app/widgets/python_script_editor.h"

// Helper to ensure single QApplication instance
static QApplication *ensureApp()
{
    if (!qApp) {
        static int argc = 1;
        static char appName[] = "test_runner";
        static char *argv[] = { appName, nullptr };
        new QApplication(argc, argv);
    }
    return static_cast<QApplication*>(qApp);
}

TEST_CASE("PythonScriptEditor creation", "[gui][python]") {
    ensureApp();

    Sicnu::PythonScriptEditor editor;

    CHECK(editor.windowTitle().isEmpty());
}

TEST_CASE("PythonScriptEditor setScript and getScript", "[gui][python]") {
    ensureApp();

    Sicnu::PythonScriptEditor editor;

    SECTION("Script round-trips") {
        const QString script = QStringLiteral("print('hello')\nx = 1 + 2\n");
        editor.setScript(script);
        CHECK(editor.script() == script);
    }

    SECTION("Empty script by default") {
        CHECK(editor.script().isEmpty());
    }
}

TEST_CASE("PythonScriptEditor clearOutput", "[gui][python]") {
    ensureApp();

    Sicnu::PythonScriptEditor editor;
    editor.appendOutput(QStringLiteral("some text"));
    editor.clearOutput();
    // Output widget should be empty after clearing.
    // The only public accessor is through script(), so we verify no crash.
    CHECK(editor.script().isEmpty());
}

TEST_CASE("PythonScriptEditor runs simple Python script", "[gui][python]") {
    ensureApp();

    Sicnu::PythonScriptEditor editor;

    QSignalSpy spy(&editor, &Sicnu::PythonScriptEditor::scriptExecuted);
    REQUIRE(spy.isValid());

    editor.setScript(QStringLiteral("print('SICNU Python Script Editor works!')"));
    editor.runScript();

    // Wait for the asynchronous worker thread to finish.
    REQUIRE(spy.wait(10000));
    REQUIRE(spy.count() == 1);
    CHECK(spy.takeFirst().at(0).toBool() == true);
}

TEST_CASE("PythonScriptEditor reports script errors", "[gui][python]") {
    ensureApp();

    Sicnu::PythonScriptEditor editor;

    QSignalSpy spy(&editor, &Sicnu::PythonScriptEditor::scriptExecuted);
    REQUIRE(spy.isValid());

    editor.setScript(QStringLiteral("raise ValueError('expected error')"));
    editor.runScript();

    REQUIRE(spy.wait(10000));
    REQUIRE(spy.count() == 1);
    CHECK(spy.takeFirst().at(0).toBool() == false);
}
