// test_teaching_admin_dock_smoke.cpp — offscreen widget smoke for the
// Teacher Authoring & Assessment Console dock (narrow teaching_admin lane:
// no QGIS, no app binary). Drives one REAL batch-grade round trip through
// the widget: the fake grader CLI (same exit/transcript contract as
// sicnu_geo_rs_cli lab --grade) must supply the verdicts — the dock itself
// must fabricate nothing.
#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"
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
#include <QRegularExpression>
#include <QTabWidget>
#include <QTemporaryDir>

#include <QElapsedTimer>
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

struct ScopedEnvironment
{
    const char *name;
    QByteArray previous;
    bool wasSet;

    ScopedEnvironment( const char *key, const QByteArray &value )
        : name( key ), previous( qgetenv( key ) ), wasSet( qEnvironmentVariableIsSet( key ) )
    {
        qputenv( name, value );
    }
    ~ScopedEnvironment()
    {
        if ( wasSet )
            qputenv( name, previous );
        else
            qunsetenv( name );
    }
};

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

TEST_CASE( "R30-T07: feedback delivery stays bound to the guarded batch snapshot",
           "[r30_t07][teaching_admin][dock][feedback_binding]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString prefix = dir.filePath( "batch" );
    const QString destination = dir.filePath( "delivery" );
    sicnu::teaching_admin::BatchAssessmentReport report;
    report.labId = QStringLiteral( "lab_feedback_A" );
    report.total = 1;
    report.graded = 1;
    sicnu::teaching_admin::BatchRowResult row;
    row.studentId = QStringLiteral( "2023110101" );
    row.labId = report.labId;
    row.status = QStringLiteral( "pass" );
    row.verdict = QStringLiteral( "pass" );
    row.score = 88.0;
    row.message = QStringLiteral( "real published batch fixture A" );
    report.rows = { row };
    REQUIRE( sicnu::teaching_admin::publishBatchOutputsAtomic( report, prefix ) );

    TeachingAdminDock dock;
    auto *out = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminBatchOut" ) );
    auto *lab = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminLabId" ) );
    auto *dest = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminFeedbackOut" ) );
    REQUIRE( out );
    REQUIRE( lab );
    REQUIRE( dest );
    out->setText( prefix );
    lab->setText( report.labId );
    dest->setText( destination );
    // Invoke the actual connected slots: loading, guarding, caching and
    // delivery are all production widget paths, not a hand-built emission.
    REQUIRE( QMetaObject::invokeMethod( &dock, "onBuildFeedbackAndSummary",
                                      Qt::DirectConnection ) );

    SECTION( "editing the next lab does not relabel the built packs" )
    {
        lab->setText( QStringLiteral( "lab_next_draft_B" ) );
        REQUIRE( QMetaObject::invokeMethod( &dock, "onDeliverFeedbackLocally",
                                          Qt::DirectConnection ) );
        REQUIRE( dock.lastFeedbackDelivery().value( QStringLiteral( "ok" ) ).toBool() );
        QFile manifestFile( dock.lastFeedbackDelivery()
                                .value( QStringLiteral( "manifest_path" ) ).toString() );
        REQUIRE( manifestFile.open( QIODevice::ReadOnly ) );
        const auto manifest = QJsonDocument::fromJson( manifestFile.readAll() ).object();
        CHECK( manifest.value( QStringLiteral( "lab_id" ) ).toString() == report.labId );
        QFile summaryFile( dock.lastFeedbackDelivery()
                               .value( QStringLiteral( "summary_path" ) ).toString() );
        REQUIRE( summaryFile.open( QIODevice::ReadOnly ) );
        const auto summary = QJsonDocument::fromJson( summaryFile.readAll() ).object();
        CHECK( summary.value( QStringLiteral( "delivery_lab_id" ) ).toString() == report.labId );
    }
    SECTION( "a later batch at the same path cannot certify cached old scores" )
    {
        report.rows[0].score = 42.0;
        report.rows[0].verdict = QStringLiteral( "fail" );
        REQUIRE( sicnu::teaching_admin::publishBatchOutputsAtomic( report, prefix ) );
        REQUIRE( QMetaObject::invokeMethod( &dock, "onDeliverFeedbackLocally",
                                          Qt::DirectConnection ) );
        CHECK_FALSE( dock.lastFeedbackDelivery().value( QStringLiteral( "ok" ) ).toBool() );
        CHECK( dock.lastFeedbackDelivery().value( QStringLiteral( "code" ) ).toString()
               == QStringLiteral( "feedback.delivery_source_changed" ) );
        CHECK_FALSE( QDir( destination ).exists() );
    }
    SECTION( "a refused rebuild does not leave earlier packs armed for delivery" )
    {
        out->setText( dir.filePath( "missing-next-batch" ) );
        REQUIRE( QMetaObject::invokeMethod( &dock, "onBuildFeedbackAndSummary",
                                          Qt::DirectConnection ) );
        REQUIRE( QMetaObject::invokeMethod( &dock, "onDeliverFeedbackLocally",
                                          Qt::DirectConnection ) );
        CHECK_FALSE( dock.lastFeedbackDelivery().value( QStringLiteral( "ok" ) ).toBool() );
        CHECK_FALSE( QDir( destination ).exists() );
    }
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
    REQUIRE( QFile::exists( fakeGraderCliPath() ) );
    ScopedEnvironment cli( "SICNU_GEO_RS_CLI", fakeGraderCliPath().toLocal8Bit() );
    ScopedEnvironment delay( "SICNU_TEST_GRADER_DELAY_MS", "1000" );

    QTemporaryDir submissions;
    REQUIRE( submissions.isValid() );
    QDir root( submissions.path() );
    // A native grader on every platform gives a full second per grade:
    // at done==8, at most 2 more items are in flight and 10+ have not started.
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
    // NOTE: the dock's default pool is 2 workers and the helper sleeps exactly
    // 1 s, so completions arrive in lockstep — done counts only even values
    // (0, 2, 4, …). Trigger on an even count.
    const QRegularExpression eighthRow( QStringLiteral( "(?<!\\d)8\\s*/\\s*20$" ) );
    const auto reachedEighthRow = [&] { return eighthRow.match( progress->text() ).hasMatch(); };
    pumpUntil( reachedEighthRow, 60000 );
    INFO( "progress label: '" << progress->text().toStdString() << "'" );
    REQUIRE( reachedEighthRow() );

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

}

TEST_CASE( "a failed batch re-arms the console instead of wedging it",
           "[teaching_admin][dock][smoke][r8]" )
{
    // #1453/WP02: the worker's exception path used to never join/reset
    // m_batchThread, so one failed batch left joinable() true forever and
    // every later click was refused with a FALSE "a batch is already
    // running". Publishing to an unwritable target now routes through the
    // same typed failure path, and the console must come back armed.
    ensureApp();
    qputenv( "SICNU_GEO_RS_CLI", fakeGraderCliPath().toLocal8Bit() );

    QTemporaryDir submissions;
    REQUIRE( submissions.isValid() );
    REQUIRE( QDir( submissions.path() ).mkdir( QStringLiteral( "s01" ) ) );
    {
        QFile good( QDir( submissions.path() ).filePath( QStringLiteral( "s01/a.tif" ) ) );
        REQUIRE( good.open( QIODevice::WriteOnly ) );
        good.write( QByteArray( "GOOD-scene" ) );
    }

    TeachingAdminDock dock;
    auto *subs = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminSubmissionsDir" ) );
    auto *out = dock.findChild<QLineEdit *>( QStringLiteral( "teachingAdminBatchOut" ) );
    auto *btn = dock.findChild<QPushButton *>( QStringLiteral( "teachingAdminBatchButton" ) );
    auto *log = dock.findChild<QPlainTextEdit *>( QStringLiteral( "teachingAdminLog" ) );
    REQUIRE( ( subs && out && btn && log ) );
    subs->setText( submissions.path() );

    // Phase 1 — the out prefix's parent directory does not exist: the
    // atomic publish fails and the run must land in the TYPED failure path
    // (a visible "batch failed"), never in a silent fake success.
    const QString badPrefix =
      QDir( submissions.path() ).filePath( QStringLiteral( "no_such_parent/batch" ) );
    out->setText( badPrefix );
    btn->click();
    pumpUntil(
      [&] { return log->toPlainText().contains( QLatin1String( "batch failed" ) ); }, 60000 );
    REQUIRE( log->toPlainText().contains( QLatin1String( "batch failed" ) ) );
    REQUIRE_FALSE( QFile::exists( badPrefix + QStringLiteral( ".json" ) ) );

    // Phase 2 — the console is re-armed: a second batch with a writable
    // prefix must actually run (the old wedge refused it with "already
    // running").
    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    const QString goodPrefix = QDir( outDir.path() ).filePath( QStringLiteral( "retry_batch" ) );
    out->setText( goodPrefix );
    btn->click();
    pumpUntil(
      [&] { return QFile::exists( goodPrefix + QStringLiteral( ".json" ) ); }, 60000 );
    REQUIRE( QFile::exists( goodPrefix + QStringLiteral( ".json" ) ) );
    pumpUntil(
      [&] { return log->toPlainText().contains( QLatin1String( "\"graded\":1" ) ); }, 10000 );
    REQUIRE( log->toPlainText().contains( QLatin1String( "\"graded\":1" ) ) );

    qunsetenv( "SICNU_GEO_RS_CLI" );
}

TEST_CASE( "TeachingAdminDock rapid destruction during batch run", "[teaching_admin][dock][rapid_destruction]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    REQUIRE( QFile::exists( fakeGraderCliPath() ) );
    ScopedEnvironment cli( "SICNU_GEO_RS_CLI", fakeGraderCliPath().toLocal8Bit() );
    ScopedEnvironment delay( "SICNU_TEST_GRADER_DELAY_MS", "2000" );
    REQUIRE( QDir( dir.path() ).mkdir( QStringLiteral( "student" ) ) );
    {
        QFile artifact( dir.filePath( QStringLiteral( "student/a.tif" ) ) );
        REQUIRE( artifact.open( QIODevice::WriteOnly ) );
        artifact.write( "GOOD-scene" );
    }
    for ( int i = 0; i < 3; ++i )
    {
        auto dock = std::make_unique<TeachingAdminDock>();
        auto *subs = dock->findChild<QLineEdit *>( QStringLiteral( "teachingAdminSubmissionsDir" ) );
        auto *out = dock->findChild<QLineEdit *>( QStringLiteral( "teachingAdminBatchOut" ) );
        auto *btn = dock->findChild<QPushButton *>( QStringLiteral( "teachingAdminBatchButton" ) );
        subs->setText( dir.path() );
        out->setText( dir.filePath( QStringLiteral( "out_%1" ).arg( i ) ) );
        btn->click();
        // Destroy immediately
        dock.reset();
    }
}
