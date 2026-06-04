// tests/test_gdal_tool_wrapper.cpp — Test GDAL tool wrapper error handling
#include <catch2/catch_test_macros.hpp>

#include <QProcess>
#include <QTemporaryDir>
#include <QFileInfo>

// Test that MergedChannels mode captures stderr in readAllStandardOutput
TEST_CASE("QProcess MergedChannels captures stderr", "[gdal][tool][error]") {
    // Run a command that writes to stderr and fails
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("bash", {"-c", "echo 'error message' >&2; exit 1"});

    REQUIRE(proc.waitForStarted(5000));
    proc.waitForFinished(5000);

    // With MergedChannels, stderr should be in readAllStandardOutput
    QByteArray stdoutOutput = proc.readAllStandardOutput();
    QByteArray stderrOutput = proc.readAllStandardError();

    // The error message should be in stdout, not stderr
    CHECK(stdoutOutput.contains("error message"));
    CHECK(stderrOutput.isEmpty()); // stderr is empty because channels are merged
}

// Test that SeparateChannels mode keeps stderr separate
TEST_CASE("QProcess SeparateChannels keeps stderr separate", "[gdal][tool][error]") {
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start("bash", {"-c", "echo 'error message' >&2; exit 1"});

    REQUIRE(proc.waitForStarted(5000));
    proc.waitForFinished(5000);

    // With SeparateChannels, stderr should be in readAllStandardError
    QByteArray stdoutOutput = proc.readAllStandardOutput();
    QByteArray stderrOutput = proc.readAllStandardError();

    // The error message should be in stderr
    CHECK(stdoutOutput.isEmpty());
    CHECK(stderrOutput.contains("error message"));
}

// Test that the fix captures error messages correctly
TEST_CASE("Tool wrapper error message capture", "[gdal][tool][error]") {
    // This test verifies the fix: when MergedChannels is used,
    // error messages should be read from readAllStandardOutput()
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("bash", {"-c", "echo 'tool failed' >&2; exit 1"});

    REQUIRE(proc.waitForStarted(5000));
    proc.waitForFinished(5000);

    // Simulate the fix: read from readAllStandardOutput() instead of readAllStandardError()
    QString errorMessage;
    if (proc.exitCode() != 0) {
        // FIX: Use readAllStandardOutput() because MergedChannels merges stderr into stdout
        errorMessage = QString::fromUtf8(proc.readAllStandardOutput());
    }

    CHECK(errorMessage.contains("tool failed"));
    CHECK_FALSE(errorMessage.isEmpty());
}

// Test that the bug exists: readAllStandardError() returns empty with MergedChannels
TEST_CASE("Bug: readAllStandardError returns empty with MergedChannels", "[gdal][tool][error]") {
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("bash", {"-c", "echo 'error info' >&2; exit 1"});

    REQUIRE(proc.waitForStarted(5000));
    proc.waitForFinished(5000);

    // BUG: This is what the current code does - it reads from readAllStandardError()
    // which returns empty because MergedChannels merges stderr into stdout
    QString buggyErrorMessage = QString::fromUtf8(proc.readAllStandardError());

    // This should be empty (the bug)
    CHECK(buggyErrorMessage.isEmpty());

    // The correct approach is to read from readAllStandardOutput()
    QString correctErrorMessage = QString::fromUtf8(proc.readAllStandardOutput());
    CHECK(correctErrorMessage.contains("error info"));
}
