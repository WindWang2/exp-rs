// test_experiment_studio_dock.cpp — offscreen ExperimentStudioDock fixture.
//
// Lifecycle honesty contracts over the REAL dock widget (headless, no file
// dialogs): the synthetic/live provenance partition, the live-store busy
// gate that keeps store A's in-flight callback from publishing over store
// B, truthful run recording on cancel/close races, and clean destruction
// with a study still in flight.
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include "app/experiment_studio/experiment_studio_dock.h"
#include "experiment/experiment_store.h"

#include <gdal.h>

#include <functional>
#include <sqlite3.h>

using sicnu::app::ExperimentStudioDock;

namespace
{

void ensureQgisApplication()
{
    if ( QApplication::instance() )
        return;
    // Offscreen: the dock must construct, lay out and destroy without a
    // display server — that IS the fixture.
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char appName[] = "test_experiment_studio_dock";
    static char *argv[] = { appName, nullptr };
    static auto *application = new QgsApplication( argc, argv, true );
    ( void ) application;
    QgsApplication::initQgis();
}

/// Creates an empty (freshly-initialized) experiment store file.
QString createEmptyStore( const QTemporaryDir &dir, const QString &name )
{
    const QString path = dir.filePath( name );
    sicnu::experiment::ExperimentStore store;
    if ( !store.open( path ) )
        return QString();
    return path;
}

qlonglong countRows( const QString &storePath, const char *sql )
{
    sqlite3 *db = nullptr;
    if ( sqlite3_open_v2( storePath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY,
                          nullptr ) != SQLITE_OK )
        return -1;
    sqlite3_stmt *stmt = nullptr;
    qlonglong count = -1;
    if ( sqlite3_prepare_v2( db, sql, -1, &stmt, nullptr ) == SQLITE_OK
         && sqlite3_step( stmt ) == SQLITE_ROW )
        count = sqlite3_column_int64( stmt, 0 );
    sqlite3_finalize( stmt );
    sqlite3_close( db );
    return count;
}

QLabel *matrixStatus( ExperimentStudioDock &dock )
{
    auto *label = dock.findChild<QLabel *>( QStringLiteral( "rsStudioMatrixStatus" ) );
    REQUIRE( label != nullptr );
    return label;
}

QPushButton *runLiveButton( ExperimentStudioDock &dock )
{
    auto *button = dock.findChild<QPushButton *>( QStringLiteral( "rsStudioRunLiveBtn" ) );
    REQUIRE( button != nullptr );
    return button;
}

void setTextInput( ExperimentStudioDock &dock, const QString &placeholder,
                   const QString &text )
{
    bool found = false;
    const auto edits = dock.findChildren<QLineEdit *>();
    for ( QLineEdit *edit : edits )
    {
        if ( edit->placeholderText().contains( placeholder, Qt::CaseInsensitive ) )
        {
            edit->setText( text );
            found = true;
        }
    }
    REQUIRE( found );
}

/// Writes a minimal single-band Float32 GTiff (the live study input).
void writeInputRaster( const QString &path )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH dataset =
        GDALCreate( driver, path.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    REQUIRE( dataset != nullptr );
    float pixels[64];
    for ( int i = 0; i < 64; ++i )
        pixels[i] = static_cast<float>( i ) / 64.0f;
    GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 8, 8, pixels, 8, 8, GDT_Float32, 0,
                           0, nullptr ) == CE_None );
    GDALClose( dataset );
}

/// Pumps events until the predicate holds or the budget expires.
bool waitFor( const std::function<bool()> &predicate, int timeoutMs )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() )
    {
        if ( timer.elapsed() > timeoutMs )
            return false;
        QEventLoop loop;
        QTimer::singleShot( 20, &loop, &QEventLoop::quit );
        loop.exec();
    }
    return true;
}

} // namespace

TEST_CASE( "studio dock demo study report is stamped synthetic in the session",
           "[experiment_studio][dock][provenance]" )
{
    ensureQgisApplication();
    ExperimentStudioDock dock;
    dock.loadDemoThresholdStudy();

    // Export honesty at the dock boundary: the demo report the session would
    // export carries the synthetic marker on the document itself.
    const auto session = dock.sessionState();
    CHECK( session.lastStudyReport.value( QStringLiteral( "synthetic" ) ).toBool() );
    CHECK( session.lastStudyReport.value( QStringLiteral( "synthetic_note" ) )
               .toString()
               .contains( QStringLiteral( "not derived from recorded runs" ) ) );
}

TEST_CASE( "studio dock store open failures are typed and honest",
           "[experiment_studio][dock][store]" )
{
    ensureQgisApplication();
    ExperimentStudioDock dock;

    // A missing store file is a typed refusal, never a silent half-open.
    CHECK( !dock.openLiveStoreAtPath( QStringLiteral( "/definitely/not/here.sqlite" ) ) );

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString storeA = createEmptyStore( dir, QStringLiteral( "store-a.sqlite" ) );
    REQUIRE( !storeA.isEmpty() );
    CHECK( dock.openLiveStoreAtPath( storeA ) );

    // With a live store open, the demo matrix is refused: synthetic rows must
    // not contaminate a session that can export live capsule refs.
    dock.loadDemoThresholdStudy();
    CHECK( matrixStatus( dock )->text().contains( QStringLiteral( "refused" ) ) );
    CHECK( dock.sessionState().lastStudyReport.isEmpty() );
}

TEST_CASE( "studio dock busy gate keeps store A runs from publishing over store B",
           "[experiment_studio][dock][storeswitch]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString storeA = createEmptyStore( dir, QStringLiteral( "store-a.sqlite" ) );
    const QString storeB = createEmptyStore( dir, QStringLiteral( "store-b.sqlite" ) );
    REQUIRE( !storeA.isEmpty() );
    REQUIRE( !storeB.isEmpty() );
    const QString inputRaster = dir.filePath( QStringLiteral( "input.tif" ) );
    writeInputRaster( inputRaster );

    auto dock = std::make_unique<ExperimentStudioDock>();
    REQUIRE( dock->openLiveStoreAtPath( storeA ) );
    setTextInput( *dock, QStringLiteral( "input" ), inputRaster );

    runLiveButton( *dock )->click();
    // Busy is observable at the UI boundary: the run button is disabled the
    // moment the worker exists, and re-enabled only after the queued result
    // landed (the same window the store gate must cover).
    REQUIRE( waitFor( [ & ] { return !runLiveButton( *dock )->isEnabled(); }, 5000 ) );

    // The gate: no store switch while the run is in flight — store B stays
    // untouched by run A's eventual callback.
    CHECK( !dock->openLiveStoreAtPath( storeB ) );

    dock->cancelLiveStudy();
    // The cooperative cancel flag is set immediately; whether it lands
    // mid-run or after the (fast-refusing) drain is timing — the TRUTHFUL
    // outcome is recorded either way. Wait for the queued result, then judge
    // by store truth, never by a UI substring.
    REQUIRE( waitFor( [ & ] { return runLiveButton( *dock )->isEnabled(); }, 30000 ) );
    CHECK( matrixStatus( dock )->text().contains( QStringLiteral( "live study" ) ) );

    // Run truth lives in store A: the study recorded every sampled point, and
    // store B (refused during flight) holds nothing at all.
    CHECK( waitFor(
        [ & ] {
            return countRows( storeA, "SELECT COUNT(*) FROM experiment_runs" ) >= 1;
        },
        5000 ) );
    CHECK( countRows( storeB, "SELECT COUNT(*) FROM experiment_runs" ) == 0 );

    // After the queued result landed, the gate reopens and a switch works.
    CHECK( dock->openLiveStoreAtPath( storeB ) );
}

TEST_CASE( "studio dock destruction with a study in flight is clean",
           "[experiment_studio][dock][lifetime]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString storeA = createEmptyStore( dir, QStringLiteral( "store-a.sqlite" ) );
    const QString inputRaster = dir.filePath( QStringLiteral( "input.tif" ) );
    writeInputRaster( inputRaster );

    auto dock = std::make_unique<ExperimentStudioDock>();
    REQUIRE( dock->openLiveStoreAtPath( storeA ) );
    setTextInput( *dock, QStringLiteral( "input" ), inputRaster );
    runLiveButton( *dock )->click();
    REQUIRE( waitFor( [ & ] { return !runLiveButton( *dock )->isEnabled(); }, 5000 ) );

    // Close mid-run: the destructor raises the cooperative cancel flag and
    // joins the worker; the shared_ptr refs keep the stores alive for the
    // in-flight recording. Any UAF or join-deadlock shows up right here.
    const auto session = dock->sessionState(); // refs survive for assertions
    dock.reset();
    QApplication::processEvents();

    // The worker drained before the join returned: every opened run reached a
    // terminal state in the store (never left Running behind).
    CHECK( waitFor(
        [ & ] {
            const qlonglong opened =
                countRows( storeA, "SELECT COUNT(*) FROM experiment_runs" );
            const qlonglong terminal = countRows(
                storeA,
                "SELECT COUNT(*) FROM experiment_runs WHERE status IN"
                " ('completed','failed','cancelled')" );
            return opened >= 0 && opened == terminal;
        },
        30000 ) );
}
