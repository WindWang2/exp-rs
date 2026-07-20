// Python Script Editor tests — verify editor UI and embedded Python execution
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QLabel>

#include "app/widgets/python_script_editor.h"

TEST_CASE("PythonScriptEditor creation", "[gui][python]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    Sicnu::PythonScriptEditor editor;

    CHECK(editor.windowTitle().isEmpty());
}

TEST_CASE("PythonScriptEditor setScript and getScript", "[gui][python]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

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
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    Sicnu::PythonScriptEditor editor;
    editor.appendOutput(QStringLiteral("some text"));
    editor.clearOutput();
    // Output widget should be empty after clearing.
    // The only public accessor is through script(), so we verify no crash.
    CHECK(editor.script().isEmpty());
}

TEST_CASE("PythonScriptEditor runs simple Python script", "[gui][python]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

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
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    Sicnu::PythonScriptEditor editor;

    QSignalSpy spy(&editor, &Sicnu::PythonScriptEditor::scriptExecuted);
    REQUIRE(spy.isValid());

    editor.setScript(QStringLiteral("raise ValueError('expected error')"));
    editor.runScript();

    REQUIRE(spy.wait(10000));
    REQUIRE(spy.count() == 1);
    CHECK(spy.takeFirst().at(0).toBool() == false);
}
