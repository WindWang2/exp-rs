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
#include <QDir>
#include <QFile>
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

/// Swaps PATH (and clears the harness pins) for the probe test; restores on
/// unwind so a failed assertion cannot leak the mutated environment into
/// other cases.
class ScopedInterpreterEnv
{
  public:
    ScopedInterpreterEnv()
        : m_path( qgetenv( "PATH" ) )
        , m_pin( qgetenv( "SICNU_PYTHON_EXECUTABLE" ) )
        , m_pin2( qgetenv( "PYTHONEXECUTABLE" ) )
    {
        qputenv( "SICNU_PYTHON_EXECUTABLE", QByteArray() );
        qputenv( "PYTHONEXECUTABLE", QByteArray() );
    }
    ~ScopedInterpreterEnv()
    {
        qputenv( "PATH", m_path );
        qputenv( "SICNU_PYTHON_EXECUTABLE", m_pin );
        qputenv( "PYTHONEXECUTABLE", m_pin2 );
    }
    void prependPath( const QString &dir )
    {
        const QString merged = QDir::toNativeSeparators( dir ) + QDir::listSeparator()
                               + QString::fromLocal8Bit( m_path );
        qputenv( "PATH", merged.toUtf8() );
    }

  private:
    QByteArray m_path;
    QByteArray m_pin;
    QByteArray m_pin2;
};

/// A fake first-on-PATH candidate with the Store alias's observable shape:
/// it launches fine but exits non-zero for any arguments.
QString writeUnusablePythonCandidate( const QString &dir )
{
#ifdef Q_OS_WIN
    // A genuine PE executable named python.exe: QProcess really starts it,
    // it fails fast on the probe's arguments, and it exits non-zero — the
    // exact launch-vs-run distinction the probe exists to make (the real
    // WindowsApps stub behaves the same way, but cannot be fabricated).
    const QString robocopy = QStandardPaths::findExecutable( QStringLiteral( "robocopy" ) );
    REQUIRE_FALSE( robocopy.isEmpty() );
    const QString fake = QDir( dir ).filePath( QStringLiteral( "python.exe" ) );
    REQUIRE( QFile::copy( robocopy, fake ) );
    return fake;
#else
    const QString fake = QDir( dir ).filePath( QStringLiteral( "python3" ) );
    QFile script( fake );
    REQUIRE( script.open( QIODevice::WriteOnly ) );
    script.write( "#!/bin/sh\nexit 42\n" );
    script.close();
    REQUIRE( QFile::setPermissions(
      fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
              | QFileDevice::ExeGroup | QFileDevice::ExeOther ) );
    return fake;
#endif
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

    // Unpinned (a real deployment): the resolver returns either an absolute
    // path whose interpreter the probe actually verified (#1474 review — a
    // candidate that merely exists on PATH, like the WindowsApps Store
    // alias, is skipped), or the typed "python3" last resort when nothing on
    // PATH can really run.
    if ( resolved != QStringLiteral( "python3" ) )
    {
        CHECK( QFileInfo( resolved ).isFile() );
        CHECK( QFileInfo( resolved ).isAbsolute() );
    }
}

TEST_CASE( "interpreter resolution skips an unusable first candidate",
           "[teaching_admin][script_adapters][r9]" )
{
    // #1474 review: findExecutable happily returns a candidate that can
    // never run a script (the WindowsApps alias stub, or any broken
    // install). The first PATH hit used to be accepted without probing,
    // hiding any working interpreter later in the candidate list. Plant the
    // unusable candidate first on a private PATH and require the resolver to
    // refuse it.
    ScopedInterpreterEnv env;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString fake = writeUnusablePythonCandidate( dir.path() );
    env.prependPath( dir.path() );

    const QString resolved = resolveDefaultPythonInterpreter();
    CHECK( QFileInfo( resolved ).absoluteFilePath() != QFileInfo( fake ).absoluteFilePath() );
    // Whatever it picked is still one of the two documented outcomes.
    if ( resolved != QStringLiteral( "python3" ) )
        CHECK( QFileInfo( resolved ).isFile() );
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

namespace
{

/// A fake repo root whose <scripts>/<name> floods stdout past the 8 MiB
/// capture cap and exits @p exitCode — the large-classroom shape.
QString writeFloodingScript( const QTemporaryDir &repo, const char *name,
                             const QByteArray &body )
{
    REQUIRE( QDir( repo.path() ).mkpath( QStringLiteral( "scripts" ) ) );
    const QString path = QDir( repo.path() ).filePath(
      QStringLiteral( "scripts/" ) + QString::fromLatin1( name ) );
    QFile script( path );
    REQUIRE( script.open( QIODevice::WriteOnly ) );
    script.write( body );
    script.close();
    return path;
}

} // namespace

TEST_CASE( "verifyOfflineBundle refuses to interpret a truncated transcript",
           "[teaching_admin][script_adapters][r9]" )
{
    if ( !harnessInterpreterPinned() )
    {
        SUCCEED( "no pinned interpreter on this machine — live subprocess section skipped" );
        return;
    }
    // #1474 review: a transcript past the capture cap is a retained head —
    // parsing it published an arbitrary prefix of the findings as if it were
    // the verifier's complete output. The exit code stays the verdict; the
    // transcript-derived fields must be refused and the loss observable.
    QTemporaryDir repo;
    REQUIRE( repo.isValid() );
    writeFloodingScript(
      repo, "verify_bundle_manifest.py",
      QByteArray( "import sys\nsys.stdout.write('x' * (8 * 1024 * 1024 + 4096))\nsys.exit(1)\n" ) );

    const BundleVerifyResult r = verifyOfflineBundle( repo.path(), repo.path() );
    REQUIRE( r.verifiable );
    REQUIRE( r.exitCode == 1 );
    CHECK( r.outputTruncated );
    CHECK( r.findings.isEmpty() );
    CHECK( r.summary == QStringLiteral( "output_truncated" ) );
    CHECK( r.toJson().value( QStringLiteral( "output_truncated" ) ).toBool() );
}

TEST_CASE( "checkLabPackDrift refuses a truncated drift list",
           "[teaching_admin][script_adapters][r9]" )
{
    if ( !harnessInterpreterPinned() )
    {
        SUCCEED( "no pinned interpreter on this machine — live subprocess section skipped" );
        return;
    }
    // Same fail-closed rule for the foundry drift check: a DRIFT list cut
    // at the capture cap would under-report real classroom drift while the
    // summary claimed a complete file count.
    QTemporaryDir repo;
    REQUIRE( repo.isValid() );
    writeFloodingScript(
      repo, "gen_lab_packs.py",
      QByteArray( "import sys\nsys.stdout.write(('DRIFT ' + 'z' * 100 + chr(10)) * 85000)\n"
                  "sys.exit(1)\n" ) );

    const PackDriftCheck check = checkLabPackDrift( repo.path() );
    REQUIRE( check.ran );
    REQUIRE( check.exitCode == 1 );
    CHECK( check.outputTruncated );
    CHECK( check.driftFiles.isEmpty() );
    CHECK_FALSE( check.inSync );
    CHECK( check.summary.contains( QStringLiteral( "truncated" ) ) );
    CHECK( check.toJson().value( QStringLiteral( "output_truncated" ) ).toBool() );
}
