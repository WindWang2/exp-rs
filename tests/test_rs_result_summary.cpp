// test_rs_result_summary.cpp — shared structured-result renderer (UX 4.0, Milestone E)
//
// One result view for operator/workflow outputs: status/context line, key
// metrics, warnings, output artifacts with add-to-map action, collapsible
// raw JSON. Replaces the per-dialog hand-rolled "success" labels.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>

#include "widgets/rs_result_summary.h"

namespace {

QApplication &testApp()
{
    static int argc = 0;
    static QApplication app( argc, nullptr );
    return app;
}

} // namespace

TEST_CASE( "RsResultSummary renders output, metrics and context",
           "[ux4][result-summary]" )
{
    testApp();
    RsResultSummary summary;

    int openRequests = 0;
    QString requestedPath;
    QObject::connect( &summary, &RsResultSummary::openPathRequested,
                      [&]( const QString &path ) {
                          ++openRequests;
                          requestedPath = path;
                      } );

    Json::Value result( Json::objectValue );
    result["output"] = "/tmp/fused.tif";
    result["mean"] = 0.42;
    result["changedPercent"] = 13.5;
    summary.setContext( QStringLiteral( "rs:image_fusion" ), 15200, true );
    summary.setResult( result );

    QLabel *status = summary.findChild<QLabel *>( QStringLiteral( "rsResultStatus" ) );
    REQUIRE( status != nullptr );
    REQUIRE( status->isVisibleTo( &summary ) );
    REQUIRE( status->text().contains( QStringLiteral( "rs:image_fusion" ) ) );
    REQUIRE( status->text().contains( QStringLiteral( "15.2" ) ) ); // elapsed
    REQUIRE( status->text().contains( QStringLiteral( "缓存命中" ) ) );

    QLabel *metrics = summary.findChild<QLabel *>( QStringLiteral( "rsResultMetrics" ) );
    REQUIRE( metrics != nullptr );
    REQUIRE( metrics->text().contains( QStringLiteral( "mean" ) ) );
    REQUIRE( metrics->text().contains( QStringLiteral( "changed" ) ) );

    QListWidget *artifacts =
        summary.findChild<QListWidget *>( QStringLiteral( "rsResultArtifacts" ) );
    REQUIRE( artifacts != nullptr );
    REQUIRE( artifacts->count() == 1 );
    REQUIRE( artifacts->item( 0 )->data( Qt::UserRole ).toString()
             == QStringLiteral( "/tmp/fused.tif" ) );

    emit artifacts->itemDoubleClicked( artifacts->item( 0 ) );
    REQUIRE( openRequests == 1 );
    REQUIRE( requestedPath == QStringLiteral( "/tmp/fused.tif" ) );

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
