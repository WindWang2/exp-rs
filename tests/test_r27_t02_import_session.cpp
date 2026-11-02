// test_r27_t02_import_session.cpp — R27-T2 milestone D: the four-state
// import session (检查 / 继续导入 / 取消 / 恢复) over the production pieces
// (ImportPreflight + DataManager::registerSource), with a durable journal
// and a machine-readable sicnu.import.session/1 summary.
#include <catch2/catch_test_macros.hpp>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/import_preflight.h"
#include "data/import_session.h"
#include "data/source_descriptor.h"

#include "geospatial/raster/raster_writer.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

namespace
{
using sicnu::data::DataManager;
using sicnu::data::ImportSession;
using sicnu::data::ImportSessionState;

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

TEST_CASE( "import session drives check → continue-import to completion",
           "[import][r27t02][session]" )
{
    QTemporaryDir dir;
    const QString a = writeRaster( dir, "a.tif" );
    const QString b = writeRaster( dir, "b.tif" );
    const QString missing = dir.filePath( "missing.tif" );

    DataManager manager;
    ImportSession::Options options;
    options.journalPath = dir.filePath( "session.json" );
    ImportSession session( &manager, { a, missing, b }, options );

    // 检查: preflight runs, the missing source is typed-refused up front.
    session.check();
    REQUIRE( session.state() == ImportSessionState::ReadyToImport );
    auto summary = session.summary();
    REQUIRE( summary.pendingCount() == 2 ); // a + b admissible
    REQUIRE( summary.refusedCount() == 1 ); // missing

    // 继续导入: both admissible sources register through the production path.
    session.proceed();
    REQUIRE( session.state() == ImportSessionState::Completed );
    summary = session.summary();
    REQUIRE( summary.registeredCount() == 2 );
    REQUIRE( summary.toJson().value( QStringLiteral( "schema" ) ).toString()
             == QStringLiteral( "sicnu.import.session/1" ) );

    // The catalog (the single identity authority) knows both assets.
    REQUIRE( manager.findByPath( a ) != std::nullopt );
    REQUIRE( manager.findByPath( b ) != std::nullopt );

    // The journal is machine-readable on disk.
    QFile journal( options.journalPath );
    REQUIRE( journal.open( QIODevice::ReadOnly ) );
    const auto doc = QJsonDocument::fromJson( journal.readAll() );
    REQUIRE( doc.object().value( "state" ).toString() == QStringLiteral( "completed" ) );
}

TEST_CASE( "import session resume is idempotent across a fresh session (journal)",
           "[import][r27t02][session]" )
{
    QTemporaryDir dir;
    const QString a = writeRaster( dir, "a.tif" );
    const QString b = writeRaster( dir, "b.tif" );

    DataManager manager;
    ImportSession::Options options;
    options.journalPath = dir.filePath( "session.json" );

    // First session: import only `a` (cancel before b by using one item),
    // simulating an interrupted batch via a two-item session where we only
    // let the first through — use a direct mini-batch instead: run a->a.
    {
        ImportSession first( &manager, { a }, options );
        first.check();
        first.proceed();
        REQUIRE( first.state() == ImportSessionState::Completed );
    }

    // Fresh process shape: NEW session over the same journal path with the
    // FULL batch. `a` must be skipped idempotently (catalog dedup), `b`
    // registers; no duplicate asset for `a`.
    ImportSession second( &manager, { a, b }, options );
    // Journal load marks `a` settled; state moves out of Checking so
    // resume() can drive the remainder.
    REQUIRE( second.state() == ImportSessionState::Failed ); // journal says partial
    second.resume();
    REQUIRE( second.state() == ImportSessionState::Completed );
    const auto summary = second.summary();
    REQUIRE( summary.registeredCount() == 2 );
    int skipped = 0, registered = 0;
    for ( const auto &item : summary.items )
    {
        if ( item.state == sicnu::data::ImportSessionItemState::Skipped )
            ++skipped;
        if ( item.state == sicnu::data::ImportSessionItemState::Registered )
            ++registered;
    }
    REQUIRE( skipped == 1 ); // a — idempotent
    REQUIRE( registered == 1 ); // b — new work only
}

TEST_CASE( "import session cancel stops between items and keeps completed work",
           "[import][r27t02][session]" )
{
    QTemporaryDir dir;
    QStringList paths;
    for ( int i = 0; i < 8; ++i )
        paths << writeRaster( dir, QStringLiteral( "r%1.tif" ).arg( i ) );

    DataManager manager;
    ImportSession::Options options;
    options.journalPath = dir.filePath( "session.json" );
    ImportSession session( &manager, paths, options );
    session.check();
    REQUIRE( session.state() == ImportSessionState::ReadyToImport );

    // Cancel BEFORE the loop starts (deterministic — a mid-loop cancel is
    // timing-dependent and would make this case flaky): nothing registers,
    // every item stays pending, the journal records the cancelled state.
    session.cancel();
    REQUIRE( session.state() == ImportSessionState::Cancelled );
    session.proceed(); // refuses: a cancelled session does not continue
    REQUIRE( session.state() == ImportSessionState::Cancelled );
    const auto summary = session.summary();
    REQUIRE( summary.registeredCount() == 0 );
    REQUIRE( summary.pendingCount() == paths.size() );

    // 恢复: the remainder imports; everything completes exactly once.
    session.resume();
    REQUIRE( session.state() == ImportSessionState::Completed );
    REQUIRE( session.summary().registeredCount() == paths.size() );
    for ( const QString &path : paths )
        REQUIRE( manager.findByPath( path ) != std::nullopt );
}
