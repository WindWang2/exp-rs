/***************************************************************************
  tests/test_teaching_admin_script_adapters_r8.cpp

  R8 (issue #1466) regressions for the teaching_admin subprocess adapters:
   * the default interpreter resolution must name something that actually
     exists on this machine (env pin, or a PATH-probed executable) — a bare
     "python3" fails every adapter with start_failed on Windows deployments
     without the Store alias;
   * runScript must bound captured stdout/stderr and report the truncation —
     a runaway classroom-batch child must not be able to OOM the admin
     console through unbounded QProcess buffers.
 ***************************************************************************/

#include "teaching_admin/script_adapters.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QFileInfo>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace sicnu::teaching_admin;

namespace
{

bool harnessInterpreterPinned()
{
    // The test harness always pins SICNU_PYTHON_EXECUTABLE
    // (cmake/SicnuTestEnv.cmake). Without a pin the fallback is
    // PATH-dependent, so live-subprocess sections skip instead of flaking.
    const QString pinned = qEnvironmentVariable( "SICNU_PYTHON_EXECUTABLE" ).trimmed();
    return !pinned.isEmpty() && QFileInfo::exists( pinned );
}

} // namespace

TEST_CASE( "interpreter resolution names a real executable when one exists",
           "[teaching_admin][script_adapters][r8]" )
{
    const QString resolved = resolveDefaultPythonInterpreter();
    const bool pinSet = !qEnvironmentVariable( "SICNU_PYTHON_EXECUTABLE" ).trimmed().isEmpty()
                        || !qEnvironmentVariable( "PYTHONEXECUTABLE" ).trimmed().isEmpty();
    if ( pinSet )
    {
        // Pinned environments are unchanged: the pin wins and points at a
        // real file (SRE-mismatch guard, #1431).
        CHECK( QFileInfo::exists( resolved ) );
        return;
    }

    // Unpinned (a real deployment): a bare "python3" is only acceptable as
    // the last resort. When any real interpreter is discoverable on PATH the
    // resolver must have returned it as an absolute path (#1466).
    const bool foundSomething =
        !QStandardPaths::findExecutable( QStringLiteral( "python" ) ).isEmpty()
        || !QStandardPaths::findExecutable( QStringLiteral( "python3" ) ).isEmpty()
        || !QStandardPaths::findExecutable( QStringLiteral( "python.exe" ) ).isEmpty();
    if ( foundSomething )
    {
        CHECK( resolved != QStringLiteral( "python3" ) );
        CHECK( QFileInfo( resolved ).isFile() );
    }
    else
    {
        CHECK( resolved == QStringLiteral( "python3" ) );
    }
}

TEST_CASE( "runScript caps captured stdout/stderr and reports the loss",
           "[teaching_admin][script_adapters][r8]" )
{
    if ( !harnessInterpreterPinned() )
    {
        SUCCEED( "no pinned interpreter on this machine — live subprocess section skipped" );
        return;
    }
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ScriptRunRequest req;
    req.program = resolveDefaultPythonInterpreter();
    req.arguments = { QStringLiteral( "-c" ),
                      QStringLiteral( "import sys\nsys.stdout.write('o' * 4096)\n"
                                      "sys.stderr.write('e' * 4096)\nsys.stdout.flush()\nsys.stderr.flush()\n" ) };
    req.workingDirectory = dir.path();
    req.timeoutMs = 60000;
    req.stdoutLimitBytes = 1024;
    req.stderrLimitBytes = 256;
    const ScriptRunResult result = runScript( req );
    REQUIRE( result.started );
    REQUIRE_FALSE( result.timedOut );
    REQUIRE_FALSE( result.crashed );
    REQUIRE( result.exitCode == 0 );
    // Head-capped capture with honest flags: the retained bytes are the true
    // prefix and the truncation is observable in the result and its JSON.
    CHECK( result.stdoutBytes.size() == 1024 );
    CHECK( result.stderrBytes.size() == 256 );
    CHECK( result.stdoutTruncated );
    CHECK( result.stderrTruncated );
    CHECK( result.stdoutBytes.startsWith( QByteArray( 32, 'o' ) ) );
    CHECK( result.stderrBytes.startsWith( QByteArray( 32, 'e' ) ) );
    const QJsonObject json = result.toJson();
    CHECK( json.value( QStringLiteral( "stdout_truncated" ) ).toBool() );
    CHECK( json.value( QStringLiteral( "stderr_truncated" ) ).toBool() );
    CHECK( json.value( QStringLiteral( "stdout_bytes" ) ).toInt() == 1024 );
}

TEST_CASE( "runScript passes small outputs through untruncated",
           "[teaching_admin][script_adapters][r8]" )
{
    if ( !harnessInterpreterPinned() )
    {
        SUCCEED( "no pinned interpreter on this machine — live subprocess section skipped" );
        return;
    }
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ScriptRunRequest req;
    req.program = resolveDefaultPythonInterpreter();
    req.arguments = { QStringLiteral( "-c" ), QStringLiteral( "print('ok')\n" ) };
    req.workingDirectory = dir.path();
    req.timeoutMs = 60000;
    const ScriptRunResult result = runScript( req );
    REQUIRE( result.started );
    REQUIRE( result.exitCode == 0 );
    CHECK( result.stdoutBytes.contains( QByteArrayLiteral( "ok" ) ) );
    CHECK_FALSE( result.stdoutTruncated );
    CHECK_FALSE( result.stderrTruncated );
    CHECK( result.toJson().value( QStringLiteral( "stdout_truncated" ) ).toBool() == false );
}
