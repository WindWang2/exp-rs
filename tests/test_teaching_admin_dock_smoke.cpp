// test_teaching_admin_dock_smoke.cpp — offscreen widget smoke for the
// Teacher Authoring & Assessment Console dock (narrow teaching_admin lane:
// no QGIS, no app binary). Drives one REAL batch-grade round trip through
// the widget: the fake grader CLI (same exit/transcript contract as
// sicnu_geo_rs_cli lab --grade) must supply the verdicts — the dock itself
// must fabricate nothing.
#include <catch2/catch_test_macros.hpp>

#include "teaching_admin_dock.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QIODevice>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>

#include <QElapsedTimer>
#include <QStandardPaths>
#include <QThread>

#include <functional>

using namespace sicnu::app::teaching_admin;

namespace {

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app )
    {
        qputenv( "QT_QPA_PLATFORM", "offscreen" );
        static int argc = 1;
        static char argv0[] = "test_teaching_admin_dock_smoke";
        static char *argv[] = { argv0, nullptr };
        app = new QApplication( argc, argv );
    }
    return app;
}

/// Pumps the event loop until @p predicate holds or @p timeoutMs elapse —
/// the batch runs on a worker thread, so the UI truth arrives via queued
/// invocations only an event pump can deliver.
void pumpUntil( const std::function<bool()> &predicate, int timeoutMs )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() && !timer.hasExpired( timeoutMs ) )
    {
        QApplication::processEvents( QEventLoop::AllEvents, 50 );
        QThread::msleep( 25 );
    }
}

/// Writes a slow (sleeping) grader stub that mirrors the sicnu.lab.grade/1
/// transcript + exit contract, for cancel-window testing. Empty string when
/// no python3 host.
QString writeSlowGraderStub( const QTemporaryDir &dir, int sleepSeconds )
{
    const QString python = QStandardPaths::findExecutable( QStringLiteral( "python3" ) );
    if ( python.isEmpty() )
        return QString();
    const QString path = QDir( dir.path() ).filePath( QStringLiteral( "slow_grader.py" ) );
    QFile script( path );
    REQUIRE( script.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    script.write( QByteArray( "#!/usr/bin/env python3\nimport json, os, sys, time\n"
                              "time.sleep( " )
                    + QByteArray::number( sleepSeconds ) + QByteArray( ")\n"
                    "args = sys.argv[1:]\n"
                    "out = args[args.index('--out')+1] if '--out' in args else ''\n"
                    "artifact = args[args.index('--grade')+1] if '--grade' in args else 'x'\n"
                    "body = {'lab_id':'slow','artifact':artifact,'rules':'<slow>',"
                    "'verdict':'pass','score':88.5,'passing_score':60.0,"
                    "'capped_by_blocking':False,'deductions':[],'evidence':[],'summary':{}}\n"
                    "doc = {'schema':'sicnu.lab.grade/1',"
                    "'digest':'0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef',"
                    "'generated_utc':'slow', 'report': body}\n"
                    "text = json.dumps(doc)\n"
                    "print(text)\n"
                    "if out:\n"
                    "    with open(out, 'w') as fh: fh.write(text)\n"
                    "sys.exit(0)\n" ) );
    script.close();
    REQUIRE( QFile::setPermissions( path, QFile::Permissions( QFile::ReadUser | QFile::WriteUser
                                                              | QFile::ExeUser ) ) );
    return path;
}

QString fakeGraderCliPath()
{
#ifdef Q_OS_WIN
    return QDir( QStringLiteral( FAKE_GRADER_CLI_DIR ) )
      .filePath( QStringLiteral( "test_teaching_fake_grader_cli.exe" ) );
#else
    return QDir( QStringLiteral( FAKE_GRADER_CLI_DIR ) )
      .filePath( QStringLiteral( "test_teaching_fake_grader_cli" ) );
#endif
}

} // namespace

TEST_CASE( "dock constructs tabs and console", "[teaching_admin][dock][smoke]" )
{
    ensureApp();
    TeachingAdminDock dock;
    auto *tabs = dock.findChild<QTabWidget *>();
    REQUIRE( tabs != nullptr );
    // A course · B lab · C rubric · D packs · E preflight · F offline ·
    // G batch · H/I feedback+summary
    REQUIRE( tabs->count() >= 8 );
    auto *log = dock.findChild<QPlainTextEdit *>( QStringLiteral( "teachingAdminLog" ) );
    REQUIRE( log != nullptr );
}

TEST_CASE( "widget batch grade round trip goes through the grader CLI contract",
           "[teaching_admin][dock][smoke]" )
{
    ensureApp();
    qputenv( "SICNU_GEO_RS_CLI", fakeGraderCliPath().toLocal8Bit() );

    QTemporaryDir submissions;
    REQUIRE( submissions.isValid() );
    REQUIRE( QDir( submissions.path() ).mkdir( QStringLiteral( "s01" ) ) );
    {
        // Scoped: flushed+closed before the grader subprocess reads it.
        QFile good( QDir( submissions.path() ).filePath( QStringLiteral( "s01/a.tif" ) ) );
        REQUIRE( good.open( QIODevice::WriteOnly ) );
        good.write( QByteArray( "GOOD-scene" ) );
    }

    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    const QString outPrefix = QDir( outDir.path() ).filePath( QStringLiteral( "smoke_batch" ) );

    TeachingAdminDock dock;
    auto *subs = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminSubmissionsDir" ) );
    auto *labId = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminLabId" ) );
    auto *out = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminBatchOut" ) );
    auto *btn = dock.findChild<QPushButton *>( QStringLiteral( "teachingAdminBatchButton" ) );
    auto *log = dock.findChild<QPlainTextEdit *>( QStringLiteral( "teachingAdminLog" ) );
    REQUIRE( ( subs && labId && out && btn && log ) );

    subs->setText( submissions.path() );
    labId->setText( QStringLiteral( "lab15_data_inspection" ) );
    out->setText( outPrefix );

    btn->click();

    // The batch grades on a worker thread; the report lands via a queued
    // UI invocation. Pump until the published outputs exist and the log
    // carries the report — a frozen UI would hang this wait, which is the
    // smoke's responsiveness oracle.
    pumpUntil( [&] {
        return QFile::exists( outPrefix + QStringLiteral( ".json" ) );
    }, 60000 );
    pumpUntil( [&] { return log->toPlainText().contains( QLatin1String( "graded" ) ); }, 10000 );

    const QString text = log->toPlainText();
    // The old fabricated scorer ("local dry-run grade", pass/80) is gone:
    // verdicts come from the grader CLI transcript only.
    REQUIRE_FALSE( text.contains( QLatin1String( "local dry-run grade" ) ) );
    REQUIRE( text.contains( QLatin1String( "\"status\":\"pass\"" ) ) );
    REQUIRE( text.contains( QLatin1String( "88.5" ) ) );
    REQUIRE( text.contains( QLatin1String( "\"graded\":1" ) ) );
    REQUIRE( text.contains( QLatin1String( "\"unavailable\":0" ) ) );
    REQUIRE( text.contains( QLatin1String( "\"grader_digest\"" ) ) );

    // Batch outputs were published atomically beside the report.
    REQUIRE( QFile::exists( outPrefix + QStringLiteral( ".json" ) ) );
    REQUIRE( QFile::exists( outPrefix + QStringLiteral( ".csv" ) ) );

    qunsetenv( "SICNU_GEO_RS_CLI" );
}

TEST_CASE( "dock batch cancel keeps completed rows and types the rest cancelled",
           "[teaching_admin][dock][smoke][cancel]" )
{
    ensureApp();
    QTemporaryDir stubDir;
    REQUIRE( stubDir.isValid() );
    const QString slowCli = writeSlowGraderStub( stubDir, 1 );
    if ( slowCli.isEmpty() )
    {
        WARN( "python3 not available; skipping UI cancel smoke" );
        return;
    }
    qputenv( "SICNU_GEO_RS_CLI", slowCli.toLocal8Bit() );

    QTemporaryDir submissions;
    REQUIRE( submissions.isValid() );
    QDir root( submissions.path() );
    // 20 x 1 s grades give a wide deterministic cancel window: when the 3rd
    // row completes, at most 2 more items are in flight and 15+ have not
    // started — the cancel flag is guaranteed to beat their start.
    constexpr int kSubmissions = 20;
    for ( int i = 1; i <= kSubmissions; ++i )
    {
        REQUIRE( root.mkdir( QStringLiteral( "s%1" ).arg( i, 2, 10, QLatin1Char( '0' ) ) ) );
        QFile f( root.filePath(
          QStringLiteral( "s%1/a.tif" ).arg( i, 2, 10, QLatin1Char( '0' ) ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "GOOD-scene" ) );
    }

    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    const QString outPrefix = QDir( outDir.path() ).filePath( QStringLiteral( "cancel_batch" ) );

    TeachingAdminDock dock;
    auto *subs = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminSubmissionsDir" ) );
    auto *labId = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminLabId" ) );
    auto *out = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminBatchOut" ) );
    auto *btn = dock.findChild<QPushButton *>( QStringLiteral( "teachingAdminBatchButton" ) );
    auto *cancelBtn =
      dock.findChild<QPushButton *>( QStringLiteral( "teachingAdminBatchCancelButton" ) );
    auto *progress = dock.findChild<QLabel *>( QStringLiteral( "teachingAdminBatchProgress" ) );
    REQUIRE( ( subs && labId && out && btn && cancelBtn && progress ) );
    REQUIRE_FALSE( cancelBtn->isEnabled() ); // idle: nothing to cancel

    subs->setText( submissions.path() );
    labId->setText( QStringLiteral( "lab15_data_inspection" ) );
    out->setText( outPrefix );

    btn->click();
    REQUIRE( cancelBtn->isEnabled() ); // running: cancellation available
    // The UI thread stays responsive while the batch grinds (20 x 1 s):
    // progress must appear without any extra pumping effort.
    // NOTE: the dock's default pool is 2 workers and the stub sleeps exactly
    // 1 s, so completions arrive in lockstep — done counts only even values
    // (0, 2, 4, …). Trigger on an even count.
    pumpUntil( [&] { return progress->text().contains( QLatin1String( "8 / 20" ) ); }, 60000 );
    INFO( "progress label: '" << progress->text().toStdString() << "'" );
    REQUIRE( progress->text().contains( QLatin1String( "8 / 20" ) ) );

    cancelBtn->click();
    pumpUntil( [&] {
        return QFile::exists( outPrefix + QStringLiteral( ".json" ) )
               && progress->text().contains( QLatin1String( "cancelled" ) );
    }, 60000 );

    // Durable partial results: the completed submission(s) are kept, the
    // never-started ones are typed cancelled — never disguised failures.
    QFile reportFile( outPrefix + QStringLiteral( ".json" ) );
    REQUIRE( reportFile.open( QIODevice::ReadOnly ) );
    const auto report = QJsonDocument::fromJson( reportFile.readAll() ).object();
    REQUIRE( report.value( QStringLiteral( "cancelled_early" ) ).toBool() );
    const int graded = report.value( QStringLiteral( "graded" ) ).toInt();
    const int cancelled = report.value( QStringLiteral( "cancelled" ) ).toInt();
    // The click landed around done==8 with <=2 items in flight: completed
    // rows are kept, everything never started is typed cancelled.
    REQUIRE( graded >= 8 );
    REQUIRE( graded <= 10 );
    REQUIRE( cancelled >= 10 );
    REQUIRE( graded + cancelled == report.value( QStringLiteral( "total" ) ).toInt() );
    for ( const auto &rv : report.value( QStringLiteral( "rows" ) ).toArray() )
    {
        const auto row = rv.toObject();
        const QString status = row.value( QStringLiteral( "status" ) ).toString();
        REQUIRE( ( status == QLatin1String( "pass" ) || status == QLatin1String( "cancelled" ) ) );
        if ( status == QLatin1String( "cancelled" ) )
            REQUIRE( row.value( QStringLiteral( "score" ) ).toDouble( -1 ) < 0.0 );
    }
    REQUIRE( QFile::exists( outPrefix + QStringLiteral( ".csv" ) ) );

    qunsetenv( "SICNU_GEO_RS_CLI" );
}
