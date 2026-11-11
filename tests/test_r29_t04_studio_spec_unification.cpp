// test_r29_t04_studio_spec_unification.cpp — D1 (R29-T04).
//
// The Study Designer's validation and the live run must consume ONE
// normalized spec: the REAL operator schema from the registry, the REAL
// input raster field, and one shared construction for dimensions/budget/
// metrics/ids. These cases pin the unification against two operators with
// DIFFERENT parameter shapes, over the real registry — no demo schema, no
// fixed /data/ndvi.tif.
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>

#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <filesystem>

#include "app/experiment_studio/experiment_studio_dock.h"
#include "experiment/experiment_store.h"
#include "operators/framework/rs_operator_registry.h"
#include "experiment_studio/study_designer.h"

#include <gdal.h>

using sicnu::app::ExperimentStudioDock;

namespace
{

void ensureQgisApplication()
{
    if ( QApplication::instance() )
        return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    QStandardPaths::setTestModeEnabled( true );
    static int argc = 1;
    static char appName[] = "test_r29_t04_studio_spec";
    static char *argv[] = { appName, nullptr };
    static auto *application = new QgsApplication( argc, argv, true );
    ( void ) application;
    QgsApplication::initQgis();
}

/// Fresh persisted-session dir per case: the dock now SAVES its session
/// (D4), so without this the cases would leak restored drafts (and inputs
/// from deleted temp dirs) into each other.
void resetStudioSessionDir( const char *tag )
{
    QCoreApplication::setApplicationName( QString::fromUtf8( tag ) );
    const QString root = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    std::filesystem::remove_all( root.toStdString() );
}

/// A real, small GTiff on disk: the designer's input gate checks EXISTENCE,
/// so "valid design" must require a real file the run could open.
QString writeRealInputRaster( const QTemporaryDir &dir )
{
    const QString path = dir.filePath( "input.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return QString();
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 8, 8, 1, GDT_Float32,
                                  nullptr );
    if ( !ds )
        return QString();
    double geoTransform[6] = { 500000.0, 1.0, 0.0, 4000000.0, 0.0, -1.0 };
    GDALSetGeoTransform( ds, geoTransform );
    GDALSetProjection( ds, "EPSG:32648" );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    const float row[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    for ( int y = 0; y < 8; ++y )
        GDALRasterIO( band, GF_Write, 0, y, 8, 1, const_cast<float *>( row ), 8, 1,
                      GDT_Float32, 0, 0 );
    GDALClose( ds );
    return QFile::exists( path ) ? path : QString();
}

QLabel *designerSummaryOf( ExperimentStudioDock *dock )
{
    return dock->findChild<QLabel *>( QStringLiteral( "rsStudioDesignerSummary" ) );
}

QLineEdit *algorithmEditOf( ExperimentStudioDock *dock )
{
    return dock->findChild<QLineEdit *>( QStringLiteral( "rsStudioAlgorithmEdit" ) );
}

QLineEdit *inputRasterEditOf( ExperimentStudioDock *dock )
{
    // The input field has no object name; it is the only editable line edit
    // besides param/metric — locate by the known placeholder.
    const auto edits = dock->findChildren<QLineEdit *>();
    for ( QLineEdit *edit : edits )
        if ( edit->placeholderText().contains( QStringLiteral( "Input raster" ) ) )
            return edit;
    return nullptr;
}

QPushButton *validateButtonOf( ExperimentStudioDock *dock )
{
    const auto buttons = dock->findChildren<QPushButton *>();
    for ( QPushButton *button : buttons )
        if ( button->text() == QLatin1String( "Validate Study Design" ) )
            return button;
    return nullptr;
}

/// Picks a second registered operator whose parameter shape differs from
/// rs:threshold_raster (a different parameter vocabulary in the real
/// registry schema).
QString secondOperatorId()
{
    const auto names = sicnu::operators::RSOperatorRegistry::instance().operatorNames();
    const std::string preferred = "rs:ndvi";
    for ( const std::string &name : names )
        if ( name == preferred )
            return QString::fromStdString( name );
    for ( const std::string &name : names )
        if ( name != "rs:threshold_raster" )
            return QString::fromStdString( name );
    return QString();
}

} // namespace

TEST_CASE( "D1: valid design requires a REAL registry algorithm and a REAL input",
           "[r29_t04][studio][d1]" )
{
    ensureQgisApplication();
    resetStudioSessionDir( __func__ );
    QTemporaryDir dir;
    const QString raster = writeRealInputRaster( dir );
    REQUIRE( !raster.isEmpty() );

    ExperimentStudioDock dock;
    auto *summary = designerSummaryOf( &dock );
    auto *algorithm = algorithmEditOf( &dock );
    auto *input = inputRasterEditOf( &dock );
    auto *validate = validateButtonOf( &dock );
    REQUIRE( ( summary && algorithm && input && validate  ) );

    SECTION( "no input → typed input_required refusal, no draft persisted" )
    {
        input->clear();
        validate->click();
        CHECK( summary->text().contains( QStringLiteral( "invalid" ) ) );
        const auto session = dock.sessionState();
        CHECK( session.designerDraft.isEmpty() );
    }

    SECTION( "nonexistent input file → typed input_missing refusal" )
    {
        input->setText( dir.filePath( "nope.tif" ) );
        validate->click();
        CHECK( summary->text().contains( QStringLiteral( "invalid" ) ) );
        CHECK( dock.sessionState().designerDraft.isEmpty() );
    }

    SECTION( "unregistered algorithm → typed algorithm_unknown refusal" )
    {
        input->setText( raster );
        // algorithmEdit is read-only in the UI (browse-driven); the spec
        // construction reads its text, so the test drives it directly.
        algorithm->setText( QStringLiteral( "rs:not_a_real_operator" ) );
        validate->click();
        CHECK( summary->text().contains( QStringLiteral( "invalid" ) ) );
        CHECK( dock.sessionState().designerDraft.isEmpty() );
    }

    SECTION( "registered algorithm + existing input → valid draft persisted" )
    {
        input->setText( raster );
        validate->click();
        INFO( summary->text().toStdString() );
        CHECK( summary->text().contains( QStringLiteral( "Valid design" ) ) );
        const auto session = dock.sessionState();
        CHECK_FALSE( session.designerDraft.isEmpty() );
        // The persisted draft carries the REAL input, not /data/ndvi.tif.
        const QJsonObject base = session.designerDraft.value( QStringLiteral( "draft_spec" ) )
                                     .toObject()
                                     .value( QStringLiteral( "base_parameters" ) )
                                     .toObject();
        CHECK( base.value( QStringLiteral( "input" ) ).toString() == raster );
    }
}

TEST_CASE( "D1: switching operators does not carry the old sweep into a foreign schema",
           "[r29_t04][studio][d1][schema]" )
{
    ensureQgisApplication();
    resetStudioSessionDir( __func__ );
    QTemporaryDir dir;
    const QString raster = writeRealInputRaster( dir );
    REQUIRE( !raster.isEmpty() );

    ExperimentStudioDock dock;
    auto *summary = designerSummaryOf( &dock );
    auto *algorithm = algorithmEditOf( &dock );
    auto *input = inputRasterEditOf( &dock );
    auto *validate = validateButtonOf( &dock );
    REQUIRE( ( summary && algorithm && input && validate  ) );
    input->setText( raster );

    // Baseline: threshold sweep over the threshold operator validates.
    algorithm->setText( QStringLiteral( "rs:threshold_raster" ) );
    validate->click();
    INFO( summary->text().toStdString() );
    REQUIRE( summary->text().contains( QStringLiteral( "Valid design" ) ) );
    REQUIRE_FALSE( dock.sessionState().designerDraft.isEmpty() );

    // Switch to an operator with a different parameter shape while the
    // swept parameter path stays "threshold": the real schema of the new
    // operator must refuse the stale dimension (unsuitable_dimension) and
    // the previous valid draft must be dropped from the session.
    const QString other = secondOperatorId();
    INFO( "second operator: " << other.toStdString() );
    REQUIRE( !other.isEmpty() );
    algorithm->setText( other );
    validate->click();
    CHECK( summary->text().contains( QStringLiteral( "invalid" ) ) );
    CHECK( dock.sessionState().designerDraft.isEmpty() );
}

TEST_CASE( "D1: a parameter range the real schema forbids never becomes a valid design",
           "[r29_t04][studio][d1][range]" )
{
    ensureQgisApplication();
    resetStudioSessionDir( __func__ );
    QTemporaryDir dir;
    const QString raster = writeRealInputRaster( dir );
    REQUIRE( !raster.isEmpty() );

    ExperimentStudioDock dock;
    auto *summary = designerSummaryOf( &dock );
    auto *algorithm = algorithmEditOf( &dock );
    auto *input = inputRasterEditOf( &dock );
    auto *validate = validateButtonOf( &dock );
    REQUIRE( ( summary && algorithm && input && validate ) );
    input->setText( raster );
    // rs:band_ratio DECLARES numeratorBand in [1,10000] (setRange in the
    // operator schema): a sweep beyond the declared range must be an
    // illegal_range draft issue — the demo schema used to bless any
    // numeric range.
    algorithm->setText( QStringLiteral( "rs:band_ratio" ) );
    QLineEdit *paramEdit = nullptr;
    for ( QLineEdit *edit : dock.findChildren<QLineEdit *>() )
        if ( edit->placeholderText().isEmpty() && !edit->isReadOnly()
             && edit != algorithm && edit != input )
            paramEdit = edit;
    REQUIRE( paramEdit != nullptr );
    paramEdit->setText( QStringLiteral( "numeratorBand" ) );
    const QList<QDoubleSpinBox *> spins = dock.findChildren<QDoubleSpinBox *>();
    REQUIRE( spins.size() >= 2 );
    // Created order minSpin→maxSpin: the LAST double spin box is the max
    // bound of the sweep.
    spins.back()->setValue( 20000.0 ); // outside schema [1,10000]
    validate->click();
    INFO( summary->text().toStdString() );
    CHECK( summary->text().contains( QStringLiteral( "invalid" ) ) );
    CHECK( dock.sessionState().designerDraft.isEmpty() );
}

TEST_CASE( "D1: two registered operators with different shapes both validate via the real schema",
           "[r29_t04][studio][d1][twoshapes]" )
{
    ensureQgisApplication();
    resetStudioSessionDir( __func__ );
    QTemporaryDir dir;
    const QString raster = writeRealInputRaster( dir );
    REQUIRE( !raster.isEmpty() );

    // Operator 1: rs:threshold_raster — numeric threshold sweep.
    {
        ExperimentStudioDock dock;
        auto *input = inputRasterEditOf( &dock );
        input->setText( raster );
        auto *validate = validateButtonOf( &dock );
        REQUIRE( validate != nullptr );
        validate->click();
        auto *summary = designerSummaryOf( &dock );
        INFO( summary->text().toStdString() );
        CHECK( summary->text().contains( QStringLiteral( "Valid design" ) ) );
        const auto draft = dock.sessionState().designerDraft;
        CHECK( draft.value( QStringLiteral( "algorithm_id" ) ).toString()
               == QStringLiteral( "rs:threshold_raster" ) );
        const auto params = draft.value( QStringLiteral( "parameters" ) ).toArray();
        bool sawThreshold = false;
        for ( const auto &v : params )
            if ( v.toObject().value( QStringLiteral( "parameter_path" ) ).toString()
                 == QStringLiteral( "threshold" ) )
                sawThreshold = true;
        CHECK( sawThreshold );
    }

    // Operator 2: a different shape — sweep a parameter that exists in ITS
    // schema. The parameter must come from the real schema; pick the first
    // sweepable one the designer itself projects.
    const QString other = secondOperatorId();
    REQUIRE( !other.isEmpty() );
    ExperimentStudioDock dock2;
    auto *input2 = inputRasterEditOf( &dock2 );
    input2->setText( raster );
    algorithmEditOf( &dock2 )->setText( other );
    // Use the designer projection directly to choose a legal sweep path for
    // this operator's own schema (the dock reads the same registry).
    const auto op = sicnu::operators::RSOperatorRegistry::instance().create(
      other.toStdString() );
    REQUIRE( op != nullptr );
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    const QByteArray bytes =
      QString::fromStdString( Json::writeString( w, op->schema() ) ).toUtf8();
    const QJsonObject schema =
      QJsonDocument::fromJson( bytes ).object();
    const auto options = sicnu::experiment_studio::projectOperatorParameters( schema );
    REQUIRE( options );
    QString sweepPath;
    for ( const auto &option : options.value() )
        if ( option.sweepable )
        {
            sweepPath = option.parameterPath;
            break;
        }
    INFO( "sweep path for " << other.toStdString() << ": " << sweepPath.toStdString() );
    if ( !sweepPath.isEmpty() )
    {
        auto *paramEdit = dock2.findChild<QLineEdit *>();
        // The parameter field is the first plain line edit (no placeholder).
        for ( QLineEdit *edit : dock2.findChildren<QLineEdit *>() )
            if ( edit->placeholderText().isEmpty() && !edit->isReadOnly() )
            {
                paramEdit = edit;
                break;
            }
        REQUIRE( paramEdit != nullptr );
        paramEdit->setText( sweepPath );
        auto *validate2 = validateButtonOf( &dock2 );
        REQUIRE( validate2 != nullptr );
        validate2->click();
        auto *summary2 = designerSummaryOf( &dock2 );
        INFO( summary2->text().toStdString() );
        CHECK( summary2->text().contains( QStringLiteral( "Valid design" ) ) );
        // And sweeping a path the operator does NOT declare is refused.
        paramEdit->setText( QStringLiteral( "threshold" ) );
        if ( sweepPath != QStringLiteral( "threshold" ) )
        {
            validate2->click();
            CHECK( summary2->text().contains( QStringLiteral( "invalid" ) ) );
        }
    }
    else
    {
        // An operator with NO sweepable parameter: the projected parameters
        // exist but none is sweepable, so the "threshold" dimension must be
        // refused as unsuitable — a directional expectation, not a
        // either-way pass.
        auto *validate2 = validateButtonOf( &dock2 );
        REQUIRE( validate2 != nullptr );
        validate2->click();
        auto *summary2 = designerSummaryOf( &dock2 );
        INFO( summary2->text().toStdString() );
        CHECK( summary2->text().contains( QStringLiteral( "invalid" ) ) );
        CHECK( dock2.sessionState().designerDraft.isEmpty() );
    }
}

TEST_CASE( "D1: runLiveStudy refuses what validation refuses (never enqueues an illegal spec)",
           "[r29_t04][studio][d1][run]" )
{
    ensureQgisApplication();
    resetStudioSessionDir( __func__ );
    QTemporaryDir dir;
    // A real store to open, and an input raster that EXISTS.
    const QString storePath = dir.filePath( "store.db" );
    {
        sicnu::experiment::ExperimentStore store;
        REQUIRE( store.open( storePath ) );
    }
    const QString raster = writeRealInputRaster( dir );
    REQUIRE( !raster.isEmpty() );

    ExperimentStudioDock dock;
    REQUIRE( dock.openLiveStoreAtPath( storePath ) );
    auto *algorithm = algorithmEditOf( &dock );
    auto *input = inputRasterEditOf( &dock );
    input->setText( raster );
    algorithm->setText( QStringLiteral( "rs:band_ratio" ) );
    QLineEdit *paramEdit = nullptr;
    for ( QLineEdit *edit : dock.findChildren<QLineEdit *>() )
        if ( edit->placeholderText().isEmpty() && !edit->isReadOnly()
             && edit != algorithm && edit != input )
            paramEdit = edit;
    REQUIRE( paramEdit != nullptr );
    paramEdit->setText( QStringLiteral( "numeratorBand" ) );

    // Illegal range for the REAL schema (numeratorBand is declared
    // [1,10000]): runLiveStudy must refuse BEFORE anything is enqueued (no
    // run rows appear in the store).
    const QList<QDoubleSpinBox *> spins = dock.findChildren<QDoubleSpinBox *>();
    REQUIRE( spins.size() >= 2 );
    spins.back()->setValue( 20000.0 ); // outside schema [1,10000]
    dock.runLiveStudy();
    sicnu::experiment::ExperimentStore check;
    REQUIRE( check.open( storePath ) );
    const auto page = check.listRuns( QString(), QString(), QString(), 0, 100 );
    if ( page )
        CHECK( page.value().second.isEmpty() ); // nothing enqueued
    // The designer log carries the typed refusal, not a silent no-op.
    const auto logs = dock.findChildren<QPlainTextEdit *>();
    bool sawTypedRefusal = false;
    for ( auto *log : logs )
        if ( log->toPlainText().contains( QStringLiteral( "experiment_studio" ) ) )
            sawTypedRefusal = true;
    CHECK( sawTypedRefusal );
}
