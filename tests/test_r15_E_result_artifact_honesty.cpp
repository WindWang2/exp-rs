// test_r15_E_result_artifact_honesty.cpp — R15-E / E05 oracle.
//
// "未存在的产物不可点击" + "失败结果不得渲染成绿色完成".
//
// Both defects were reproduced on master 46791bc65c6f552ebbcf723bb91892736d56f319:
//   * RsResultSummary::rebuildUi() built a clickable QListWidgetItem for every
//     "output"/"outputs" string without ever checking QFileInfo::exists(), and
//     itemDoubleClicked emitted openPathRequested unconditionally — the shell
//     then routed a non-existent path into loadRasterLayer().
//   * the status line was a hard-coded "✓ Processing finished" that ignored a
//     result whose own "status" member said it failed.
//
// These cases are the RED proof; they fail against the pre-fix widget.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTemporaryDir>

#include "widgets/rs_result_summary.h"

namespace
{

QApplication &testApp()
{
    static int argc = 0;
    static QApplication app( argc, nullptr );
    return app;
}

QListWidget *artifactList( RsResultSummary &summary )
{
    return summary.findChild<QListWidget *>( QStringLiteral( "rsResultArtifacts" ) );
}

/// Writes a 1-byte placeholder so the path really exists on disk.
QString makeRealFile( const QTemporaryDir &dir, const QString &name )
{
    const QString path = dir.filePath( name );
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "x" );
    f.close();
    REQUIRE( QFileInfo::exists( path ) );
    return path;
}

} // namespace

TEST_CASE( "RsResultSummary refuses to open artifacts that do not exist",
           "[r15e][e05][result-summary]" )
{
    testApp();
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString real = makeRealFile( dir, QStringLiteral( "real.tif" ) );
    const QString missing = dir.filePath( QStringLiteral( "never_written.tif" ) );
    REQUIRE( !QFileInfo::exists( missing ) );

    RsResultSummary summary;
    int openRequests = 0;
    QString requestedPath;
    QObject::connect( &summary, &RsResultSummary::openPathRequested,
                      [&]( const QString &path ) {
                          ++openRequests;
                          requestedPath = path;
                      } );

    Json::Value outputs( Json::arrayValue );
    outputs.append( real.toStdString() );
    outputs.append( missing.toStdString() );
    Json::Value result( Json::objectValue );
    result["outputs"] = outputs;
    summary.setResult( result );

    QListWidget *artifacts = artifactList( summary );
    REQUIRE( artifacts != nullptr );
    REQUIRE( artifacts->count() == 2 );

    // (1) The existing artifact keeps its behaviour exactly.
    QListWidgetItem *okItem = artifacts->item( 0 );
    REQUIRE( okItem->flags() & Qt::ItemIsEnabled );
    REQUIRE( okItem->data( Qt::UserRole ).toString() == real );
    emit artifacts->itemDoubleClicked( okItem );
    REQUIRE( openRequests == 1 );
    REQUIRE( requestedPath == real );

    // (2) The missing artifact is inert: disabled, no UserRole, no signal.
    QListWidgetItem *badItem = artifacts->item( 1 );
    REQUIRE_FALSE( badItem->flags() & Qt::ItemIsEnabled );
    REQUIRE( badItem->data( Qt::UserRole ).toString().isEmpty() );
    REQUIRE( badItem->text().contains( missing ) );
    REQUIRE_FALSE( badItem->toolTip().isEmpty() );
    emit artifacts->itemDoubleClicked( badItem );
    REQUIRE( openRequests == 1 ); // unchanged — nothing was requested
}

TEST_CASE( "RsResultSummary hides the success mark when the result itself failed",
           "[r15e][e05][result-summary]" )
{
    testApp();
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    RsResultSummary summary;
    Json::Value result( Json::objectValue );
    result["status"] = "failed";
    result["mean"] = 0.1;
    summary.setResult( result );

    QLabel *status = summary.findChild<QLabel *>( QStringLiteral( "rsResultStatus" ) );
    REQUIRE( status != nullptr );
    REQUIRE( status->isVisibleTo( &summary ) );
    REQUIRE_FALSE( status->text().contains( QStringLiteral( "✓" ) ) );
    REQUIRE( status->property( "state" ).toString() == QStringLiteral( "error" ) );
}

TEST_CASE( "RsResultSummary refuses the success mark for every failure spelling",
           "[r15e][e05][result-summary]" )
{
    testApp();
    for ( const char *spelling : { "failed", "fail", "error", "aborted", "cancelled",
                                   "Failed", " FAILED " } )
    {
        RsResultSummary summary;
        Json::Value result( Json::objectValue );
        result["status"] = spelling;
        result["mean"] = 0.1;
        summary.setResult( result );

        QLabel *status = summary.findChild<QLabel *>( QStringLiteral( "rsResultStatus" ) );
        REQUIRE( status != nullptr );
        INFO( "status spelling: " << spelling );
        CHECK_FALSE( status->text().contains( QStringLiteral( "✓" ) ) );
        CHECK( status->property( "state" ).toString() == QStringLiteral( "error" ) );
    }
}

TEST_CASE( "RsResultSummary keeps the success mark for a passing result",
           "[r15e][e05][result-summary]" )
{
    testApp();
    RsResultSummary summary;
    Json::Value result( Json::objectValue );
    result["status"] = "completed";
    result["mean"] = 0.1;
    summary.setResult( result );

    QLabel *status = summary.findChild<QLabel *>( QStringLiteral( "rsResultStatus" ) );
    REQUIRE( status != nullptr );
    REQUIRE( status->text().contains( QStringLiteral( "✓" ) ) );
    REQUIRE( status->property( "state" ).toString() == QStringLiteral( "ok" ) );
}
