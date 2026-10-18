// test_rs_result_summary.cpp — shared structured-result renderer (UX 4.0, Milestone E)
//
// One result view for operator/workflow outputs: status/context line, key
// metrics, warnings, output artifacts with add-to-map action, collapsible
// raw JSON. Replaces the per-dialog hand-rolled "success" labels.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTemporaryDir>

#include "widgets/rs_result_summary.h"

namespace {

QApplication &testApp()
{
    static int argc = 0;
    static QApplication app( argc, nullptr );
    return app;
}

// R15-E: this case used to feed a literal "/tmp/fused.tif" that does not
// exist and then assert that double-clicking it emitted openPathRequested.
// That assertion pinned the defect this file now guards against (a missing
// artifact must be inert), so the fixture writes a real file instead.
QString realTempTif( QTemporaryDir &dir, const QString &name )
{
    const QString path = dir.filePath( name );
    QFile f( path );
    if ( f.open( QIODevice::WriteOnly ) )
    {
        f.write( "x" );
        f.close();
    }
    return path;
}

} // namespace

TEST_CASE( "RsResultSummary renders output, metrics and context",
           "[ux4][result-summary]" )
{
    testApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString fusedPath = realTempTif( dir, QStringLiteral( "fused.tif" ) );
    REQUIRE( QFileInfo::exists( fusedPath ) );

    RsResultSummary summary;

    int openRequests = 0;
    QString requestedPath;
    QObject::connect( &summary, &RsResultSummary::openPathRequested,
                      [&]( const QString &path ) {
                          ++openRequests;
                          requestedPath = path;
                      } );

    Json::Value result( Json::objectValue );
    result["output"] = fusedPath.toStdString();
    result["mean"] = 0.42;
    result["changedPercent"] = 13.5;
    summary.setContext( QStringLiteral( "rs:image_fusion" ), 15200, true );
    summary.setResult( result );

    QLabel *status = summary.findChild<QLabel *>( QStringLiteral( "rsResultStatus" ) );
    REQUIRE( status != nullptr );
    REQUIRE( status->isVisibleTo( &summary ) );
    REQUIRE( status->text().contains( QStringLiteral( "rs:image_fusion" ) ) );
    REQUIRE( status->text().contains( QStringLiteral( "15.2" ) ) ); // elapsed
    REQUIRE( status->text().contains( QStringLiteral( "Cache Hit" ) ) );

    QLabel *metrics = summary.findChild<QLabel *>( QStringLiteral( "rsResultMetrics" ) );
    REQUIRE( metrics != nullptr );
    REQUIRE( metrics->text().contains( QStringLiteral( "mean" ) ) );
    REQUIRE( metrics->text().contains( QStringLiteral( "changed" ) ) );

    QListWidget *artifacts =
        summary.findChild<QListWidget *>( QStringLiteral( "rsResultArtifacts" ) );
    REQUIRE( artifacts != nullptr );
    REQUIRE( artifacts->count() == 1 );
    REQUIRE( artifacts->item( 0 )->data( Qt::UserRole ).toString() == fusedPath );

    emit artifacts->itemDoubleClicked( artifacts->item( 0 ) );
    REQUIRE( openRequests == 1 );
    REQUIRE( requestedPath == fusedPath );

    // Raw JSON is collapsible and off by default.
    QPlainTextEdit *raw = summary.findChild<QPlainTextEdit *>( QStringLiteral( "rsResultRawJson" ) );
    REQUIRE( raw != nullptr );
    REQUIRE( !raw->isVisibleTo( &summary ) );
    REQUIRE( raw->toPlainText().contains( QStringLiteral( "fused.tif" ) ) );
}

TEST_CASE( "RsResultSummary renders warnings and multi-output lists",
           "[ux4][result-summary]" )
{
    testApp();
    RsResultSummary summary;

    Json::Value outputs( Json::arrayValue );
    outputs.append( "/tmp/b1.tif" );
    outputs.append( "/tmp/b2.tif" );
    Json::Value warnings( Json::arrayValue );
    warnings.append( "Band 12 contains NoData rows" );

    Json::Value result( Json::objectValue );
    result["outputs"] = outputs;
    result["warnings"] = warnings;
    summary.setResult( result );

    QListWidget *artifacts =
        summary.findChild<QListWidget *>( QStringLiteral( "rsResultArtifacts" ) );
    REQUIRE( artifacts != nullptr );
    REQUIRE( artifacts->count() == 2 );

    QLabel *warningsLabel =
        summary.findChild<QLabel *>( QStringLiteral( "rsResultWarnings" ) );
    REQUIRE( warningsLabel != nullptr );
    REQUIRE( warningsLabel->isVisibleTo( &summary ) );
    REQUIRE( warningsLabel->text().contains( QStringLiteral( "NoData" ) ) );
}

TEST_CASE( "RsResultSummary clear() resets to the empty state",
           "[ux4][result-summary]" )
{
    testApp();
    RsResultSummary summary;

    Json::Value result( Json::objectValue );
    result["output"] = "/tmp/x.tif";
    summary.setResult( result );
    REQUIRE( summary.hasResult() );

    summary.clear();
    REQUIRE( !summary.hasResult() );
    QListWidget *artifacts =
        summary.findChild<QListWidget *>( QStringLiteral( "rsResultArtifacts" ) );
    REQUIRE( artifacts != nullptr );
    REQUIRE( artifacts->count() == 0 );
    QLabel *status = summary.findChild<QLabel *>( QStringLiteral( "rsResultStatus" ) );
    REQUIRE( status != nullptr );
    REQUIRE( !status->isVisibleTo( &summary ) );
}
