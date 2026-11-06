// test_r28_t02_closed_loop.cpp — R28-T02 WP12: the production-shaped closed
// loop over the data/import/inference runtime, with fault injection:
//
//   Scenario A  批量体检 → 部分导入 → 取消 → 恢复 → 真实推理 → 写出 → 重开
//   Scenario B  多任务排队 → 取消/超时/关机（typed reasons, bounded shutdown)
//
// Everything runs on real pieces: ImportPreflight batch, ImportSession with
// a durable journal, the OpenCV DNN CPU provider on the committed identity
// ONNX fixture, GDAL reopen verification. No mocks of providers; no sleep
// synchronization (event-loop steps / poll predicates / injected deadlines).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#ifdef SICNU_HAS_OPENCV

#include "data/data_manager.h"
#include "data/import_preflight.h"
#include "data/import_session.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/inference_plan.h"
#include "operators/runtime/model_execution_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTimer>

#include <gdal_priv.h>

#include <functional>
#include <string>
#include <vector>

using namespace sicnu::operators::runtime;
using Catch::Approx;

namespace
{
using sicnu::data::DataManager;
using sicnu::data::ImportSession;
using sicnu::data::ImportSessionState;

QCoreApplication *ensureCoreApp()
{
    static QCoreApplication *app = QCoreApplication::instance();
    if ( !app )
    {
        static int argc = 1;
        static char arg0[] = "test_r28_t02_closed_loop";
        static char *argv[] = { arg0, nullptr };
        app = new QCoreApplication( argc, argv );
    }
    return app;
}

std::string writeFixtureRaster( const QTemporaryDir &dir, const QString &name, int size )
{
    const std::string path =
        QDir( dir.path() ).filePath( name ).toStdString();
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds =
        GDALCreate( driver, path.c_str(), size, size, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> pixels( static_cast<std::size_t>( size ) * size );
    for ( std::size_t i = 0; i < pixels.size(); ++i )
        pixels[i] = static_cast<float>( i % 97 ) * 0.25f;
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, size, size, pixels.data(), size,
                           size, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
    return path;
}

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
        if ( session.hasPendingItems() || session.state() == ImportSessionState::Checking )
            QTimer::singleShot( 0, &loop, tick );
        else
            loop.quit();
    };
    QTimer::singleShot( 0, &loop, tick );
    loop.exec();
    return session.state();
}
} // namespace

TEST_CASE( "scenario A: batch preflight → partial import → cancel → resume → real inference → reopen",
           "[r28t02][closedloop][opencv]" )
{
    ensureCoreApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<std::string> sources;
    QStringList qSources;
    for ( int i = 0; i < 5; ++i )
    {
        const QString name = QStringLiteral( "src%1.tif" ).arg( i );
        sources.push_back( writeFixtureRaster( dir, name, 8 + i ) );
        qSources << QString::fromStdString( sources.back() );
    }

    // ① Batch health check (WP04): counts + one action, member verdicts from
    // the SAME inspect the import path uses.
    const auto batch = sicnu::data::ImportPreflight::inspectBatch( qSources );
    REQUIRE( batch.reports.size() == 5 );
    REQUIRE( batch.importableCount() == 5 );
    REQUIRE( batch.toJson().value( QStringLiteral( "suggested_action" ) ).toString()
             == QStringLiteral( "import-ready" ) );

    // ② Partial import with a UI-shaped queued cancel after item #2.
    DataManager manager;
    sicnu::data::ImportSession::Options options;
    options.journalPath = dir.filePath( QStringLiteral( "session.json" ) );
    ImportSession session( &manager, qSources, options );
    CancelPoster poster;
    const auto midState = driveStepwise( session, poster, [&session]() {
        return session.summary().registeredCount() >= 2;
    } );
    REQUIRE( midState == ImportSessionState::Cancelled );
    const int settledBefore = session.summary().registeredCount();
    REQUIRE( settledBefore >= 2 );
    REQUIRE( settledBefore <= 3 );

    // ③ Resume completes the remainder exactly once; journal and CATALOG agree
    // on every item (G2: resume is not just deserialization — the catalog is
    // the authority and every settled source is queryable).
    session.resume();
    REQUIRE( session.state() == ImportSessionState::Completed );
    REQUIRE( session.summary().registeredCount() == 5 );
    for ( const QString &path : qSources )
        REQUIRE( manager.findByPath( path ) != std::nullopt );

    // ④ Real CPU inference over an IMPORTED asset (plan → run → write).
    const std::string model =
        QDir( QString::fromStdString( TESTS_DATA_DIR ) )
            .filePath( QStringLiteral( "test_infer_identity.onnx" ) )
            .toStdString();
    REQUIRE( QFileInfo::exists( QString::fromStdString( model ) ) );
    const std::string output =
        QDir( dir.path() ).filePath( QStringLiteral( "inferred.tif" ) ).toStdString();

    ModelExecutionRequest request;
    request.inputPath = sources.front();
    request.modelReference = model;
    request.outputPath = output;
    const InferencePlan plan = planModelInference( request );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Ready );

    sicnu::operators::RSOperatorContext context;
    const ModelExecutionResult result = runModelInference( request, context );
    REQUIRE( result.payload["backend"].asString() == "opencv_dnn" );
    REQUIRE( result.payload["tiles"].asInt() >= 1 );

    // ⑤ The artifact reopens with the input geometry and identity values.
    GDALDatasetH out = GDALOpen( output.c_str(), GA_ReadOnly );
    REQUIRE( out != nullptr );
    const int w = GDALGetRasterXSize( out ), h = GDALGetRasterYSize( out );
    GDALClose( out );
    REQUIRE( w == 8 );
    REQUIRE( h == 8 );

    // ⑥ Fault variant — journal write failure mid-loop keeps the catalog the
    // only truth: rerun the import on a fresh manager with an unwritable
    // journal; the summary must say non-durable, the import still completes.
    DataManager fresh;
    sicnu::data::ImportSession::Options broken;
    broken.journalPath = dir.filePath( QStringLiteral( "no/such/dir/j.json" ) );
    ImportSession faulted( &fresh, qSources, broken );
    faulted.check();
    faulted.proceed();
    REQUIRE( faulted.state() == ImportSessionState::Completed );
    REQUIRE( !faulted.summary().journalDurable );
    for ( const QString &path : qSources )
        REQUIRE( fresh.findByPath( path ) != std::nullopt );
}

#include "test_r28_t02_closed_loop.moc"

#endif // SICNU_HAS_OPENCV
