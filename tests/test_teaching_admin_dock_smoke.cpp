// test_teaching_admin_dock_smoke.cpp — offscreen widget smoke for the
// Teacher Authoring & Assessment Console dock (narrow teaching_admin lane:
// no QGIS, no app binary). Drives one REAL batch-grade round trip through
// the widget: the fake grader CLI (same exit/transcript contract as
// sicnu_geo_rs_cli lab --grade) must supply the verdicts — the dock itself
// must fabricate nothing.
#include <catch2/catch_test_macros.hpp>

#include "teaching_admin_dock.h"

#include <QApplication>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>

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
