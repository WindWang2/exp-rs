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
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

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
        char *argv[] = { a0, nullptr };
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
    dialog.runCheck();

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

    // Import only the admissible item (headless driver: the button click
    // would run the same path through accepted()).
    REQUIRE( dialog.importButton()->isEnabled() );
    dialog.runImport();
    CHECK( dialog.registeredPaths() == QStringList{ good } );
    CHECK( dialog.refusedPaths().size() == 1 );
    CHECK( manager.findByPath( good ).has_value() );
    CHECK_FALSE( manager.findByPath( missing ).has_value() );

    // Summary line explains the batch outcome in one sentence.
    CHECK( dialog.summaryLabel()->text().contains( QStringLiteral( "1" ) ) );
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
    first.runCheck();
    first.runImport();
    REQUIRE( first.registeredPaths().size() == 1 );

    sicnu::app::ImportBatchDialog second( &manager, { raster }, nullptr, journal );
    second.runCheck();
    second.runImport();

    // The catalog still holds exactly one asset for the path, and the
    // second run says WHY: already registered.
    CHECK( manager.assets().size() == 1 );
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
        abandoned.runCheck();
    }
    const QStringList pending =
        sicnu::app::ImportBatchDialog::pendingPathsFromJournal( journal );
    REQUIRE( pending.size() == 2 );
    CHECK( pending.contains( a ) );
    CHECK( pending.contains( b ) );

    // The resume entry reconstructs the batch from the journal and finishes
    // it: both register exactly once (idempotent across the fresh session).
    sicnu::app::ImportBatchDialog resume( &manager, pending, nullptr, journal );
    resume.runCheck();
    resume.runImport();
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
    dialog.runCheck();
    dialog.runImport();

    // The registration itself still happened — the catalog is the truth —
    // but the summary says the durable resume point is missing.
    CHECK( dialog.registeredPaths() == QStringList{ raster } );
    CHECK( dialog.summaryLabel()->text().contains( QStringLiteral( "journal" ),
                                                    Qt::CaseInsensitive ) );
}
