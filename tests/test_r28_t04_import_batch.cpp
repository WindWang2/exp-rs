// test_r28_t04_import_batch.cpp — WP03: the Add-Raster/Vector batch flow
// surfaces T02's ImportPreflight/ImportSession state to the user.
//
// The old loop registered every file synchronously and reported only
// "N succeeded, M failed" in the status bar — no per-item reason, no
// needs-choice handling, no dedup explanation, no resume entry. The
// ImportBatchDialog drives the EXISTING session (check → proceed →
// journal) read-only: this suite pins the presentation contracts.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>

#include "app/dialogs/import_batch_dialog.h"
#include "data/data_manager.h"
#include "data/import_preflight.h"
#include "data/import_session.h"

#include "geospatial/raster/raster_writer.h"

namespace
{

void ensureApp()
{
    if ( !qApp )
    {
        qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
        static int argc = 1;
        static char a0[] = "test_r28_t04_import_batch";
        static char *argv[] = { a0, nullptr };
        new QApplication( argc, argv );
    }
}

QString writeRaster( const QTemporaryDir &dir, const QString &name )
{
    const QString path = dir.filePath( name );
    sicnu::geo::RasterBandSpec spec;
    spec.dtype = "Float32";
    sicnu::geo::RasterWriteOptions options;
    options.overwrite = true;
    sicnu::geo::RasterWriter writer =
        sicnu::geo::RasterWriter::create( path.toStdString(), 8, 8, { spec }, options );
    writer.finalize();
    return path;
}


void waitForPrimaryAction( sicnu::app::ImportBatchDialog &dialog )
{
    QElapsedTimer timeout;
    timeout.start();
    while ( !dialog.importButton()->isEnabled() && timeout.elapsed() < 5000 )
        QCoreApplication::processEvents();
    REQUIRE( dialog.importButton()->isEnabled() );
}

void showAndCheck( sicnu::app::ImportBatchDialog &dialog )
{
    dialog.show();
    waitForPrimaryAction( dialog );
}

void importAndWait( sicnu::app::ImportBatchDialog &dialog )
{
    dialog.importButton()->click();
    waitForPrimaryAction( dialog );
    REQUIRE( dialog.importButton()->text() == QStringLiteral( "Close" ) );
}

} // namespace

TEST_CASE( "import batch dialog shows per-item verdicts and typed refusals",
           "[r28t04][import][batch][verdicts]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString good = writeRaster( dir, QStringLiteral( "good.tif" ) );
    const QString missing = dir.filePath( QStringLiteral( "missing.tif" ) );

    sicnu::data::DataManager manager;
    sicnu::app::ImportBatchDialog dialog(
        &manager, { good, missing }, nullptr,
        dir.filePath( QStringLiteral( "journal.json" ) ) );
    showAndCheck( dialog );

    REQUIRE( dialog.itemsView()->count() == 2 );
    const bool missingRowTyped = [ & ] {
        for ( int i = 0; i < dialog.itemsView()->count(); ++i )
        {
            const QString text = dialog.itemsView()->item( i )->text();
            if ( text.contains( QStringLiteral( "missing.tif" ) ) )
                return text.contains( QStringLiteral( "Refused" ),
                                      Qt::CaseInsensitive )
                       && !text.contains( QStringLiteral( "Failed" ) );
        }
        return false;
    }();
    CHECK( missingRowTyped );

    // Real two-click UI path: first click imports and keeps the per-item
    // detail visible; second click emits accepted() for exec() callers.
    // Assert the signal rather than the modal result, which QDialog resets
    // when no exec() loop is active in a headless Catch2 binary.
    REQUIRE( dialog.importButton()->isEnabled() );
    bool accepted = false;
    QObject::connect( &dialog, &QDialog::accepted, [ & ]() { accepted = true; } );
    importAndWait( dialog );
    CHECK( dialog.isVisible() );
    CHECK_FALSE( accepted );
    dialog.importButton()->click();
    CHECK( accepted );

    CHECK( dialog.registeredPaths() == QStringList{ good } );
    CHECK( dialog.refusedPaths().size() == 1 );
    CHECK( manager.findByPath( good ).has_value() );
    CHECK_FALSE( manager.findByPath( missing ).has_value() );

    // Summary line explains the batch outcome with real counts, not a
    // vacuous "contains 1" assertion.
    const QString summary = dialog.summaryLabel()->text();
    CHECK( summary.contains( QStringLiteral( "1 registered" ) ) );
    CHECK( summary.contains( QStringLiteral( "1 refused" ) ) );
}

TEST_CASE( "re-importing the same source explains the dedup instead of duplicating",
           "[r28t04][import][batch][dedup]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString raster = writeRaster( dir, QStringLiteral( "dup.tif" ) );

    sicnu::data::DataManager manager;
    const QString journal = dir.filePath( QStringLiteral( "journal.json" ) );

    sicnu::app::ImportBatchDialog first( &manager, { raster }, nullptr, journal );
    showAndCheck( first );
    importAndWait( first );
    REQUIRE( first.registeredPaths().size() == 1 );
    first.importButton()->click();

    sicnu::app::ImportBatchDialog second( &manager, { raster }, nullptr, journal );
    showAndCheck( second );
    importAndWait( second );

    // The catalog still holds exactly one asset for the path, and the
    // second run says WHY: already registered.
    CHECK( manager.assets().size() == 1 );
    CHECK( second.displayableAssets() == first.displayableAssets() );
    CHECK( second.summaryLabel()->text().contains( QStringLiteral( "1 reused" ) ) );
    bool dedupExplained = false;
    for ( int i = 0; i < second.itemsView()->count(); ++i )
    {
        const QString text = second.itemsView()->item( i )->text();
        if ( text.contains( QStringLiteral( "dup.tif" ) )
             && text.contains( QStringLiteral( "already" ), Qt::CaseInsensitive ) )
        {
            dedupExplained = true;
        }
    }
    CHECK( dedupExplained );
}

TEST_CASE( "journal pending paths feed the resume entry end to end",
           "[r28t04][import][batch][resume]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString a = writeRaster( dir, QStringLiteral( "a.tif" ) );
    const QString b = writeRaster( dir, QStringLiteral( "b.tif" ) );

    sicnu::data::DataManager manager;
    const QString journal = dir.filePath( QStringLiteral( "journal.json" ) );

    // First batch checked but abandoned before importing (user cancelled
    // the dialog): the journal keeps both items Pending.
    {
        sicnu::app::ImportBatchDialog abandoned( &manager, { a, b }, nullptr, journal );
        showAndCheck( abandoned );
        abandoned.reject();
    }
    const QStringList pending =
        sicnu::app::ImportBatchDialog::pendingPathsFromJournal( journal );
    REQUIRE( pending.size() == 2 );
    CHECK( pending.contains( a ) );
    CHECK( pending.contains( b ) );

    // The resume entry reconstructs the batch from the journal and finishes
    // it: both register exactly once (idempotent across the fresh session).
    sicnu::app::ImportBatchDialog resume( &manager, pending, nullptr, journal );
    showAndCheck( resume );
    importAndWait( resume );
    CHECK( resume.registeredPaths().size() == 2 );
    CHECK( manager.findByPath( a ).has_value() );
    CHECK( manager.findByPath( b ).has_value() );
}

TEST_CASE( "journal write failure is surfaced, not swallowed",
           "[r28t04][import][batch][journal_honesty]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString raster = writeRaster( dir, QStringLiteral( "c.tif" ) );
    // A journal path that cannot be published (a directory stands where the
    // file must be written).
    QDir().mkdir( dir.filePath( QStringLiteral( "blocked.json" ) ) );

    sicnu::data::DataManager manager;
    sicnu::app::ImportBatchDialog dialog( &manager, { raster }, nullptr,
                                          dir.filePath( QStringLiteral( "blocked.json" ) ) );
    showAndCheck( dialog );
    importAndWait( dialog );

    // The registration itself still happened — the catalog is the truth —
    // but the summary says the durable resume point is missing.
    CHECK( dialog.registeredPaths() == QStringList{ raster } );
    CHECK( dialog.summaryLabel()->text().contains( QStringLiteral( "journal" ),
                                                    Qt::CaseInsensitive ) );
}

TEST_CASE( "a new batch checks its own files even with an existing journal",
           "[r28t04][import][batch][preflight]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString journal = dir.filePath( QStringLiteral( "journal.json" ) );
    const QString raster = writeRaster( dir, QStringLiteral( "first.tif" ) );
    sicnu::data::DataManager manager;
    {
        sicnu::app::ImportBatchDialog first( &manager, { raster }, nullptr, journal );
        showAndCheck( first );
        first.reject();
        CHECK( first.result() == QDialog::Rejected );
    }

    const QString missing = dir.filePath( QStringLiteral( "missing.tif" ) );
    sicnu::app::ImportBatchDialog second( &manager, { missing, raster }, nullptr, journal );
    showAndCheck( second );
    CHECK( second.refusedPaths() == QStringList{ missing } );
    CHECK( second.summaryLabel()->text().contains( QStringLiteral( "1 ready" ) ) );
    CHECK( manager.assets().isEmpty() );
}

TEST_CASE( "Escape and window close preserve completed batch results",
           "[r28t04][import][batch][dismissal]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString raster = writeRaster( dir, QStringLiteral( "committed.tif" ) );
    sicnu::data::DataManager manager;
    sicnu::app::ImportBatchDialog dialog( &manager, { raster }, nullptr,
                                          dir.filePath( QStringLiteral( "journal.json" ) ) );
    bool useEscape = true;
    SECTION( "Escape" ) {}
    SECTION( "window close" ) { useEscape = false; }

    QTimer interaction;
    QObject::connect( &interaction, &QTimer::timeout, &dialog, [&] {
        if ( !dialog.importButton()->isEnabled() )
            return;
        if ( dialog.importButton()->text() != QStringLiteral( "Close" ) )
        {
            dialog.importButton()->click();
            return;
        }
        if ( useEscape )
        {
            QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
            QCoreApplication::sendEvent( &dialog, &escape );
        }
        else
            dialog.close();
    } );
    QTimer::singleShot( 5000, &dialog, [&] { dialog.done( -1 ); } );
    interaction.start( 1 );
    CHECK( dialog.exec() == QDialog::Accepted );
    CHECK( dialog.displayableAssets().size() == 1 );
}

TEST_CASE( "stopping import keeps pending work and shows the outcome",
           "[r28t04][import][batch][stop]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString raster = writeRaster( dir, QStringLiteral( "pending.tif" ) );
    const QString journal = dir.filePath( QStringLiteral( "journal.json" ) );
    sicnu::data::DataManager manager;
    sicnu::app::ImportBatchDialog dialog( &manager, { raster }, nullptr, journal );
    showAndCheck( dialog );

    dialog.importButton()->click();
    dialog.reject();
    CHECK( dialog.isVisible() );
    CHECK( dialog.importButton()->text() == QStringLiteral( "Close" ) );
    CHECK( dialog.summaryLabel()->text().contains( QStringLiteral( "1 pending" ) ) );
    CHECK( sicnu::app::ImportBatchDialog::pendingPathsFromJournal( journal ) == QStringList{ raster } );
    CHECK( manager.assets().isEmpty() );
}
