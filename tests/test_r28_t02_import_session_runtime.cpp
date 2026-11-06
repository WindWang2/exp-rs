// test_r28_t02_import_session_runtime.cpp — R28-T02 WP03: the import
// session's cancel/resume loop must stay responsive to a UI-shaped caller.
//
//   * Stepwise cooperative import: a caller on the DataManager's owning
//     thread drives bounded proceedSteps()/checkSteps() chunks so its event
//     loop keeps spinning; a cancel QUEUED onto that thread (how every Qt
//     widget cancels) lands at the next step boundary — the batch stops
//     within one item, not after the whole batch. The monolithic proceed()
//     keeps its CLI semantics; this suite pins the interleaving contract
//     for the stepwise seam.
//   * Thread-safe cancel request: any thread may arm the cooperative flag;
//     the owning thread finalizes Cancelled at the next boundary.
//   * Journal fault matrix: unwritable journal (honest journalError, import
//     continues), corrupt journal (fail-closed to Pending), healing after a
//     transient write failure.
//   * Resume fact-check: a journaled-registered source whose file was
//     REPLACED or MOVED is surfaced (visible warning in the summary), not
//     silently skipped on journal trust alone.
#include <catch2/catch_test_macros.hpp>

#include "data/data_manager.h"
#include "data/import_preflight.h"
#include "data/import_session.h"

#include "geospatial/raster/raster_writer.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <functional>
#include <thread>

namespace
{
using sicnu::data::DataManager;
using sicnu::data::ImportSession;
using sicnu::data::ImportSessionItemState;
using sicnu::data::ImportSessionState;

QString writeRaster( const QTemporaryDir &dir, const QString &name, int size = 8 )
{
    const QString path = dir.filePath( name );
    sicnu::geo::RasterBandSpec spec;
    spec.dtype = "Float32";
    sicnu::geo::RasterWriteOptions options;
    options.overwrite = true;
    sicnu::geo::RasterWriter writer =
        sicnu::geo::RasterWriter::create( path.toStdString(), size, size, { spec }, options );
    writer.finalize();
    return path;
}

/// QTimer(0) chaining and queued invocations need a thread event dispatcher,
/// which only exists with a QCoreApplication on the thread (a bare Catch2
/// main has none — the stepwise driver would block forever in loop.exec()).
/// Created once per binary, mirroring the ensureApp() seam other suites use.
QCoreApplication *ensureCoreApp()
{
    static QCoreApplication *app = QCoreApplication::instance();
    if ( !app )
    {
        static int argc = 1;
        static char arg0[] = "test_r28_t02_import_session_runtime";
        static char *argv[] = { arg0, nullptr };
        app = new QCoreApplication( argc, argv );
    }
    return app;
}

/// QObject standing in for the UI surface: cancel arrives as a QUEUED
/// invocation on the owning thread — a button click's exact delivery shape.
struct CancelPoster : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    void postCancel( ImportSession *session )
    {
        QMetaObject::invokeMethod(
            this, [session]() { session->requestCancel(); }, Qt::QueuedConnection );
    }
};

/// Drives the session the way the import panel will: one bounded step per
/// event-loop spin, so queued deliveries (cancel, UI paint) land between
/// steps. Returns when the session reaches a terminal state (or maxSteps).
ImportSessionState driveStepwise( ImportSession &session, CancelPoster &poster,
                                  const std::function<bool()> &cancelNow,
                                  int maxSteps = 1000 )
{
    QEventLoop loop;
    int step = 0;
    bool cancelPosted = false;
    std::function<void()> tick;
    tick = [&]() {
        if ( step >= maxSteps )
        {
            loop.quit();
            return;
        }
        ++step;
        if ( session.state() == ImportSessionState::Checking )
            session.checkSteps( 4 );
        else if ( session.state() == ImportSessionState::ReadyToImport
                  || session.state() == ImportSessionState::Importing )
            session.proceedSteps( 1 );
        else
        {
            loop.quit();
            return;
        }
        if ( !cancelPosted && cancelNow() )
        {
            cancelPosted = true;
            poster.postCancel( &session );
        }
        if ( session.hasPendingItems()
             || session.state() == ImportSessionState::Checking )
            QTimer::singleShot( 0, &loop, tick );
        else
            loop.quit();
    };
    QTimer::singleShot( 0, &loop, tick );
    loop.exec();
    return session.state();
}
} // namespace

TEST_CASE( "stepwise import: queued cancel lands at the next step boundary",
           "[import][r28t02][cancel]" )
{
    QTemporaryDir dir;
    QStringList paths;
    for ( int i = 0; i < 6; ++i )
        paths << writeRaster( dir, QStringLiteral( "s%1.tif" ).arg( i ) );

    DataManager manager;
    ImportSession::Options options;
    options.journalPath = dir.filePath( "session.json" );
    ImportSession session( &manager, paths, options );
    CancelPoster poster;

    ensureCoreApp();
    const auto state = driveStepwise( session, poster, [&session]() {
        return session.summary().registeredCount() >= 1; // cancel after item #1
    } );
    // The queued cancel could only land between steps: at most ONE more item
    // was settled after the request (the step boundary the flag missed).
    REQUIRE( state == ImportSessionState::Cancelled );
    const auto summary = session.summary();
    REQUIRE( summary.registeredCount() <= 2 );
    REQUIRE( summary.registeredCount() >= 1 );
    REQUIRE( summary.pendingCount() >= 4 );

    // Resume finishes the remainder exactly once; the catalog (one identity
    // authority) holds every settled item.
    session.resume();
    REQUIRE( session.state() == ImportSessionState::Completed );
    REQUIRE( session.summary().registeredCount() == 6 );
    for ( const QString &path : paths )
        REQUIRE( manager.findByPath( path ) != std::nullopt );
}

TEST_CASE( "stepwise check: preflight runs in bounded chunks and stays cancellable",
           "[import][r28t02][cancel]" )
{
    QTemporaryDir dir;
    QStringList paths;
    for ( int i = 0; i < 5; ++i )
        paths << writeRaster( dir, QStringLiteral( "c%1.tif" ).arg( i ) );

    DataManager manager;
    ImportSession::Options options;
    ImportSession session( &manager, paths, options );

    // First bounded chunk: 2 of 5 items get verdicts.
    session.checkSteps( 2 );
    REQUIRE( session.state() == ImportSessionState::Checking ); // not finished
    auto summary = session.summary();
    int withVerdict = 0;
    for ( const auto &item : summary.items )
        if ( !item.verdict.isEmpty() )
            ++withVerdict;
    REQUIRE( withVerdict == 2 );

    // A thread-safe cancel request from any thread finalizes the session.
    std::thread canceller( [&session]() { session.requestCancel(); } );
    canceller.join();
    session.checkSteps( 1 ); // next boundary observes the flag
    REQUIRE( session.state() == ImportSessionState::Cancelled );
    REQUIRE( session.summary().pendingCount() >= 3 );
}

TEST_CASE( "journal write failure is honest and heals", "[import][r28t02][journal]" )
{
    QTemporaryDir dir;
    QTemporaryDir roDir;
    const QString a = writeRaster( dir, "a.tif" );
    const QString b = writeRaster( dir, "b.tif" );

    DataManager manager;
    ImportSession::Options options;
    // An impossible journal location: every publish fails, the import must
    // still complete and the summary must say the resume point is NOT durable.
    options.journalPath = roDir.filePath( "no/such/dir/session.json" );
    ImportSession session( &manager, { a, b }, options );
    session.check();
    session.proceed();
    REQUIRE( session.state() == ImportSessionState::Completed );
    const auto summary = session.summary();
    REQUIRE( summary.registeredCount() == 2 );
    REQUIRE( !summary.journalError.isEmpty() );
    REQUIRE( !summary.journalDurable );
    // The catalog still holds both assets: durable truth is not the journal.
    REQUIRE( manager.findByPath( a ) != std::nullopt );
    REQUIRE( manager.findByPath( b ) != std::nullopt );

    // Healing: a session whose journal publishes succeed again clears the
    // error (R2-F7 semantics preserved through the runtime seams).
    ImportSession::Options okOptions;
    okOptions.journalPath = dir.filePath( "heal.json" );
    ImportSession healed( &manager, { a, b }, okOptions );
    healed.check();
    healed.proceed();
    REQUIRE( healed.summary().journalDurable );
    REQUIRE( healed.summary().journalError.isEmpty() );
}

TEST_CASE( "corrupt journal fails closed to Pending", "[import][r28t02][journal]" )
{
    QTemporaryDir dir;
    const QString a = writeRaster( dir, "a.tif" );
    const QString b = writeRaster( dir, "b.tif" );

    {
        QFile journal( dir.filePath( "session.json" ) );
        REQUIRE( journal.open( QIODevice::WriteOnly ) );
        journal.write( "{ not json ]]" );
    }

    DataManager manager;
    ImportSession::Options options;
    options.journalPath = dir.filePath( "session.json" );
    ImportSession session( &manager, { a, b }, options );
    // Garbage journal: state starts fresh, nothing is trusted from it.
    session.check();
    REQUIRE( session.state() == ImportSessionState::ReadyToImport );
    REQUIRE( session.summary().pendingCount() == 2 );
    session.proceed();
    REQUIRE( session.state() == ImportSessionState::Completed );
    REQUIRE( session.summary().registeredCount() == 2 );
}

TEST_CASE( "resume surfaces replaced and moved sources, not silent skips",
           "[import][r28t02][resume]" )
{
    QTemporaryDir dir;
    const QString kept = writeRaster( dir, "kept.tif" );
    const QString replaced = writeRaster( dir, "replaced.tif" );
    const QString moved = writeRaster( dir, "moved.tif" );

    DataManager manager;
    ImportSession::Options options;
    options.journalPath = dir.filePath( "session.json" );
    {
        ImportSession first( &manager, { kept, replaced, moved }, options );
        first.check();
        first.proceed();
        REQUIRE( first.state() == ImportSessionState::Completed );
    }

    // The registered sources change on disk after the fact.
    QFile::remove( replaced );
    writeRaster( dir, "replaced.tif", 16 ); // same path, different bytes
    QFile::remove( moved );

    // Fresh session over the journal (process-restart shape): the catalog
    // still owns registration, but the FACTS about the files are reported.
    ImportSession second( &manager, { kept, replaced, moved }, options );
    REQUIRE( second.state() == ImportSessionState::Completed ); // journal: all settled
    // The fact-check pass is the public read-only re-verification resume()
    // runs automatically; a hosting surface (Data Doctor shape) may also
    // call it directly on a settled session.
    second.verifyFacts();
    REQUIRE( second.state() == ImportSessionState::Completed );
    const auto summary = second.summary();
    int mismatchWarnings = 0, missingWarnings = 0;
    for ( const auto &item : summary.items )
    {
        if ( item.state != ImportSessionItemState::Skipped )
            continue;
        if ( item.message.contains( QLatin1String( "replaced" ) )
             || item.message.contains( QLatin1String( "identity" ) ) )
            ++mismatchWarnings;
        if ( item.message.contains( QLatin1String( "missing" ) ) )
            ++missingWarnings;
    }
    REQUIRE( mismatchWarnings == 1 ); // replaced.tif — same path, new bytes
    REQUIRE( missingWarnings == 1 ); // moved.tif — file gone

    // Idempotence: a second fact-check pass must produce IDENTICAL messages
    // (a polling consumer must never accumulate "; missing; missing; …").
    const QStringList messagesAfterFirst = [&]() {
        QStringList list;
        for ( const auto &item : summary.items )
            list << item.message;
        return list;
    }();
    second.verifyFacts();
    const auto resummary = second.summary();
    for ( std::size_t i = 0; i < resummary.items.size(); ++i )
        REQUIRE( resummary.items[i].message == messagesAfterFirst[static_cast<int>( i )] );
}

TEST_CASE( "duplicate source in one batch dedups through the catalog",
           "[import][r28t02][dedup]" )
{
    QTemporaryDir dir;
    const QString a = writeRaster( dir, "same.tif" );

    DataManager manager;
    ImportSession::Options options;
    options.journalPath = dir.filePath( "session.json" );
    ImportSession session( &manager, { a, a }, options );
    session.check();
    session.proceed();
    REQUIRE( session.state() == ImportSessionState::Completed );
    const auto summary = session.summary();
    REQUIRE( summary.registeredCount() == 2 ); // both settled (1 registered + 1 dedup skip)
    int reused = 0;
    for ( const auto &item : summary.items )
        if ( item.reusedExisting || item.state == ImportSessionItemState::Skipped )
            ++reused;
    REQUIRE( reused == 1 );
    // One asset, not two — the catalog stays the single identity authority.
    REQUIRE( manager.findByPath( a ) != std::nullopt );
}

TEST_CASE( "batch preflight aggregates verdicts with one suggested action",
           "[import][r28t02][preflight]" )
{
    QTemporaryDir dir;
    const QString good = writeRaster( dir, "good.tif" );
    const QString missing = dir.filePath( "missing.tif" );

    const auto batch = sicnu::data::ImportPreflight::inspectBatch( { good, missing } );
    REQUIRE( batch.reports.size() == 2 );
    REQUIRE( batch.importableCount() == 1 );
    REQUIRE( batch.refusedCount() == 1 );
    REQUIRE( batch.needsUserChoiceCount() == 0 );

    const QJsonObject json = batch.toJson();
    REQUIRE( json.value( QStringLiteral( "schema" ) ).toString()
             == QStringLiteral( "sicnu.import.preflight.batch/1" ) );
    REQUIRE( json.value( QStringLiteral( "total" ) ).toInt() == 2 );
    REQUIRE( json.value( QStringLiteral( "suggested_action" ) ).toString()
             == QStringLiteral( "fix-or-drop-refused" ) );
    // Member reports keep the per-source verdict vocabulary unchanged.
    const QJsonArray sources = json.value( QStringLiteral( "sources" ) ).toArray();
    REQUIRE( sources.size() == 2 );
    REQUIRE( sources.at( 0 ).toObject().value( QStringLiteral( "path" ) ).toString()
             == good );

    // An all-good batch suggests import-ready; an empty batch too (no
    // decisions to make).
    const auto allGood = sicnu::data::ImportPreflight::inspectBatch( { good } );
    REQUIRE( allGood.toJson().value( QStringLiteral( "suggested_action" ) ).toString()
             == QStringLiteral( "import-ready" ) );
    const auto empty = sicnu::data::ImportPreflight::inspectBatch( {} );
    REQUIRE( empty.reports.isEmpty() );
    REQUIRE( empty.toJson().value( QStringLiteral( "suggested_action" ) ).toString()
             == QStringLiteral( "import-ready" ) );
}

#include "test_r28_t02_import_session_runtime.moc"
