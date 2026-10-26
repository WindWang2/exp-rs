// tests/test_r23_e_native_terminal.cpp — R23-E WP5 contract guard for the
// #1618 fix family (commit 1b3d28f1): QgsLinuxNative::openTerminalAtPath
// must spawn the environment-selected terminal with the REQUESTED DIRECTORY
// as the process working directory (the QProcess::startDetached cwd seam),
// never via --working-directory/--workdir argv (konsole says --workdir, xterm
// has no such option — that was the defect). The probe terminals below
// record argv AND pwd so both halves of the contract are asserted.
#include <catch2/catch_test_macros.hpp>

#include "native/linux/qgslinuxnative.h"

#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

/// Restores one environment variable on scope exit.
class EnvGuard
{
public:
    explicit EnvGuard( const char *name )
        : mName( name )
    {
        const char *current = ::getenv( name );
        mHadValue = current != nullptr;
        mValue = current ? current : "";
    }
    ~EnvGuard()
    {
        if ( mHadValue )
            qputenv( mName, mValue.toUtf8() );
        else
            qunsetenv( mName );
    }

private:
    const char *mName;
    bool mHadValue = false;
    QString mValue;
};

/// Writes one probe terminal script into @p binDir. The script records its
/// argv ($0 on line 1, "$@" on line 2) and its working directory (line 3) so
/// the test can prove BOTH the terminal selection and the cwd contract.
void writeProbeTerminal( const std::filesystem::path &binDir, const char *name )
{
    std::filesystem::create_directories( binDir );
    const std::filesystem::path script = binDir / name;
    std::ofstream out( script );
    out << "#!/bin/sh\n"
        << "{ echo \"$0\"; echo \"$@\"; pwd; } > \"$SICNU_R23E_TERM_PROBE\"\n";
    out.close();
    std::filesystem::permissions( script,
                                  std::filesystem::perms::owner_all
                                    | std::filesystem::perms::group_all
                                    | std::filesystem::perms::others_all );
}

/// The three probe lines: binary, args, cwd.
struct ProbeResult
{
    QString binary;
    QString args;
    QString cwd;
    bool valid = false;
};

/// Waits (bounded) for the probe file, then parses its three lines.
ProbeResult waitForProbe( const std::filesystem::path &probe, int timeoutMs = 5000 )
{
    for ( int elapsed = 0; elapsed < timeoutMs; elapsed += 25 )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
        std::ifstream in( probe );
        if ( in.good() )
        {
            ProbeResult result;
            std::string binary, args, cwd;
            std::getline( in, binary );
            std::getline( in, args );
            std::getline( in, cwd );
            if ( !cwd.empty() )
            {
                result.binary = QString::fromStdString( binary );
                result.args = QString::fromStdString( args );
                result.cwd = QString::fromStdString( cwd );
                result.valid = true;
            }
            return result;
        }
    }
    return {};
}

struct ProbeEnv
{
    QTemporaryDir scratch;
    std::filesystem::path binDir;
    std::filesystem::path workDir;
    std::filesystem::path probe;

    explicit ProbeEnv( const char *probeLeaf = "term_probe.txt" )
    {
        binDir = std::filesystem::path( scratch.path().toStdString() ) / "bin";
        workDir = std::filesystem::path( scratch.path().toStdString() ) / "target-dir";
        probe = std::filesystem::path( scratch.path().toStdString() ) / probeLeaf;
        std::filesystem::create_directories( workDir );
        for ( const char *name : { "xterm", "x-terminal-emulator", "gnome-terminal", "konsole" } )
            writeProbeTerminal( binDir, name );
        qputenv( "SICNU_R23E_TERM_PROBE", probe.string().c_str() );
    }
};

} // namespace

TEST_CASE( "openTerminalAtPath launches the selected terminal with the path as its cwd (#1618)",
           "[r23e][native][linux]" )
{
    ProbeEnv env;
    EnvGuard pathGuard( "PATH" );
    EnvGuard sessionGuard( "DESKTOP_SESSION" );
    EnvGuard desktopGuard( "XDG_CURRENT_DESKTOP" );
    EnvGuard gdmGuard( "GDMSESSION" );

    qputenv( "PATH", ( env.binDir.string() + ":" + ( ::getenv( "PATH" ) ? ::getenv( "PATH" ) : "" ) ).c_str() );
    qunsetenv( "DESKTOP_SESSION" );
    qunsetenv( "XDG_CURRENT_DESKTOP" );
    qunsetenv( "GDMSESSION" );

    // Branch selection mirrors the production order: debian_version wins,
    // then gnome/unity, then kde, else xterm. The test stays deterministic
    // on every distro by asserting the branch this host actually takes.
    const bool isDebian = QFile::exists( QStringLiteral( "/etc/debian_version" ) );
    const QString expectedTerminal = isDebian ? QStringLiteral( "x-terminal-emulator" )
                                              : QStringLiteral( "xterm" );

    QgsLinuxNative native;
    REQUIRE( native.openTerminalAtPath( QString::fromStdString( env.workDir.string() ) ) );

    const ProbeResult probe = waitForProbe( env.probe );
    INFO( "probe binary: " << probe.binary.toStdString() << " args: '"
                           << probe.args.toStdString() << "' cwd: "
                           << probe.cwd.toStdString() );
    REQUIRE( probe.valid );

    // Terminal selection: the probe's $0 names the binary that ran (Qt may
    // pass the program as given or fully resolved — accept either spelling).
    const bool binaryMatches = probe.binary == expectedTerminal
                               || probe.binary.endsWith( QStringLiteral( "/" ) + expectedTerminal );
    REQUIRE( binaryMatches );

    // cwd contract (#1618): the probe process runs IN the requested dir.
    REQUIRE( probe.cwd.toStdString() == env.workDir.string() );

    // argv contract (#1618): NO --working-directory/--workdir flags.
    REQUIRE( probe.args.trimmed().isEmpty() );
}

TEST_CASE( "openTerminalAtPath honors the KDE desktop session selector",
           "[r23e][native][linux]" )
{
    if ( QFile::exists( QStringLiteral( "/etc/debian_version" ) ) )
    {
        // On debian-family hosts the x-terminal-emulator branch wins before
        // the desktop-session selectors are consulted; that ordering is the
        // production contract, so the KDE leg has nothing to observe here.
        SUCCEED( "debian host: desktop-session branches are unreachable by design" );
        return;
    }

    ProbeEnv env( "term_probe_kde.txt" );
    EnvGuard pathGuard( "PATH" );
    EnvGuard sessionGuard( "DESKTOP_SESSION" );
    EnvGuard desktopGuard( "XDG_CURRENT_DESKTOP" );
    EnvGuard gdmGuard( "GDMSESSION" );

    qputenv( "PATH", ( env.binDir.string() + ":" + ::getenv( "PATH" ) ).c_str() );
    qunsetenv( "DESKTOP_SESSION" );
    qputenv( "XDG_CURRENT_DESKTOP", "KDE" );
    qunsetenv( "GDMSESSION" );

    QgsLinuxNative native;
    REQUIRE( native.openTerminalAtPath( QString::fromStdString( env.workDir.string() ) ) );

    const ProbeResult probe = waitForProbe( env.probe );
    INFO( "probe binary: " << probe.binary.toStdString() << " args: '"
                           << probe.args.toStdString() << "' cwd: "
                           << probe.cwd.toStdString() );
    REQUIRE( probe.valid );
    const bool konsoleMatched = probe.binary == QStringLiteral( "konsole" )
                                || probe.binary.endsWith( QStringLiteral( "/konsole" ) );
    REQUIRE( konsoleMatched );
    REQUIRE( probe.args.trimmed().isEmpty() );
    REQUIRE( probe.cwd.toStdString() == env.workDir.string() );
}

TEST_CASE( "linux capabilities advertise the terminal seam", "[r23e][native][linux]" )
{
    QgsLinuxNative native;
    REQUIRE( native.capabilities() & QgsNative::NativeOpenTerminalAtPath );
}
