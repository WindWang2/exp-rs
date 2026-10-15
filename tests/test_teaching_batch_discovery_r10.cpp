/***************************************************************************
  tests/test_teaching_batch_discovery_r10.cpp — R10-B (#1466 tail):
  batch discovery SKIP ACCOUNTING.

  The discovery heuristics in batch_assessment.cpp deliberately refuse
  tool-output names (grades*, *batch*, *summary*, *feedback*) so a rerun
  does not grade its own report. That refusal must be VISIBLE: a student
  file named zhang_feedback.json may not vanish from the class report
  without a trace. Every refused file is accounted in the published JSON
  (skipped_files[] + skipped count) and as a status="skipped" CSV row —
  the batch result count stays honest: graded rows + explicit refusals.

  Phase discipline: this file first ran against UNFIXED master with only
  the JSON/CSV-level assertions (RED evidence in the R10-B ledger); the
  in-memory API assertions were added together with the fix.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/json_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

using namespace sicnu::teaching_admin;

namespace
{

struct DiscoveryTree
{
    QTemporaryDir dir;
    QDir submissions;

    // Path -> bytes; subdirectories are created on demand.
    DiscoveryTree( const QStringList &relativeFiles )
    {
        submissions = dir.path() + QStringLiteral( "/submissions" );
        submissions.mkpath( QStringLiteral( "." ) );
        for ( const QString &rel : relativeFiles )
        {
            const QFileInfo fi( submissions.filePath( rel ) );
            QDir().mkpath( fi.absolutePath() );
            QFile f( fi.absoluteFilePath() );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( "payload" );
        }
    }

    QString checkpointPath() const { return dir.path() + QStringLiteral( "/ckpt.json" ); }
    QString outPrefix() const { return dir.path() + QStringLiteral( "/report" ); }
};

BatchAssessmentConfig discoveryConfig( const QString &submissionsDir, const QString &checkpointPath )
{
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab_r10b" );
    cfg.submissionsDir = submissionsDir;
    cfg.outDir = submissionsDir;
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "v1" );
    cfg.softwareVersion = QStringLiteral( "test" );
    cfg.maxConcurrency = 1;
    cfg.maxSubmissions = -1;
    cfg.checkpointPath = checkpointPath;
    return cfg;
}

BatchRowResult passRow( const SubmissionItem &item )
{
    BatchRowResult row;
    row.studentId = item.studentId;
    row.artifactPath = item.path;
    row.status = QStringLiteral( "pass" );
    row.verdict = QStringLiteral( "pass" );
    row.score = 90.0;
    return row;
}

QJsonObject readJson( const QString &path )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    return QJsonDocument::fromJson( f.readAll() ).object();
}

/// (path-suffix, student_id) pairs found in skipped_files[], in file order.
struct SkipEntry
{
    QString pathSuffix;
    QString studentId;
    QString reason;
};

QVector<SkipEntry> skipEntries( const QJsonObject &doc )
{
    QVector<SkipEntry> out;
    const QJsonArray arr = doc.value( QStringLiteral( "skipped_files" ) ).toArray();
    for ( const auto &v : arr )
    {
        const QJsonObject o = v.toObject();
        out.push_back( SkipEntry{ o.value( QStringLiteral( "path" ) ).toString(),
                                  o.value( QStringLiteral( "student_id" ) ).toString(),
                                  o.value( QStringLiteral( "reason" ) ).toString() } );
    }
    return out;
}

/// Count skip rows AND verify their column structure against the header —
/// counting the "skipped" token alone would pass on a row with the wrong
/// number of cells (R1 review P2).
int csvSkippedRows( const QString &path, int expectedColumns )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    QTextStream in( &f );
    // Skip the BOM the publisher writes as bytes.
    if ( !in.atEnd() )
        in.seek( 0 );
    QString header = in.readLine();
    REQUIRE( header.startsWith( QStringLiteral( "student_id," ) ) );
    const int headerColumns = header.split( QLatin1Char( ',' ) ).size();
    REQUIRE( headerColumns == expectedColumns );

    int count = 0;
    while ( !in.atEnd() )
    {
        const QString line = in.readLine();
        if ( !line.contains( QLatin1String( ",\"skipped\"," ) ) )
            continue;
        ++count;
        // Cell count must match the header; a quoted comma inside the
        // reason text is not split (esc() doubles quotes, never commas).
        REQUIRE( line.split( QLatin1Char( ',' ) ).size() == expectedColumns );
    }
    return count;
}

} // namespace

TEST_CASE( "r10 discovery accounting: reserved-name files are refused visibly, not vanished",
           "[teaching_r10][batch]" )
{
    DiscoveryTree tree( { QStringLiteral( "zhang/artifact_1.tif" ),
                          QStringLiteral( "zhang/zhang_feedback.json" ),
                          QStringLiteral( "li/artifact_2.tif" ),
                          QStringLiteral( "grades.csv" ) } );
    const BatchAssessmentReport report = runBatchAssessment(
      discoveryConfig( tree.submissions.path(), tree.checkpointPath() ),
      []( const SubmissionItem &item ) { return passRow( item ); } );

    // The grading set itself is unchanged: two graded artifacts.
    CHECK( report.total == 2 );
    CHECK( report.graded == 2 );

    REQUIRE( publishBatchOutputsAtomic( report, tree.outPrefix() ) );
    const QJsonObject doc = readJson( tree.outPrefix() + QStringLiteral( ".json" ) );

    // Every refused file is accounted with a reason. On unfixed master the
    // key is absent and this REQUIRE fails — the silent-vanish RED.
    const QVector<SkipEntry> skips = skipEntries( doc );
    REQUIRE( skips.size() == 2 );
    CHECK( doc.value( QStringLiteral( "skipped" ) ).toInt() == 2 );

    bool sawFeedback = false, sawGrades = false;
    for ( const SkipEntry &e : skips )
    {
        CHECK( !e.reason.isEmpty() );
        if ( e.pathSuffix.endsWith( QLatin1String( "zhang_feedback.json" ) ) )
        {
            sawFeedback = true;
            // The refusal is attributable: it happened inside zhang's folder.
            CHECK( e.studentId == QStringLiteral( "zhang" ) );
        }
        if ( e.pathSuffix.endsWith( QLatin1String( "grades.csv" ) ) )
            sawGrades = true;
    }
    CHECK( sawFeedback );
    CHECK( sawGrades );

    // The teacher's gradebook (CSV) carries the refusals as typed rows too —
    // a student visible only through a skip must not be invisible in Excel.
    CHECK( csvSkippedRows( tree.outPrefix() + QStringLiteral( ".csv" ), 13 ) == 2 );
}

TEST_CASE( "r10 discovery accounting: a skip-only student is an explicit refusal",
           "[teaching_r10][batch]" )
{
    // wang submitted one file whose name trips the feedback heuristic. On
    // unfixed master the whole run is a typed EMPTY report — wang does not
    // exist anywhere in the outputs.
    DiscoveryTree tree( { QStringLiteral( "wang/wang_feedback.json" ) } );
    const BatchAssessmentReport report = runBatchAssessment(
      discoveryConfig( tree.submissions.path(), tree.checkpointPath() ),
      []( const SubmissionItem & ) {
          FAIL( "nothing may be graded — the only file is refused" );
          return BatchRowResult{};
      } );

    CHECK( report.total == 0 );
    CHECK( report.rows.isEmpty() );

    REQUIRE( publishBatchOutputsAtomic( report, tree.outPrefix() ) );
    const QJsonObject doc = readJson( tree.outPrefix() + QStringLiteral( ".json" ) );
    const QVector<SkipEntry> skips = skipEntries( doc );
    REQUIRE( skips.size() == 1 );
    CHECK( skips.front().studentId == QStringLiteral( "wang" ) );
    CHECK( skips.front().pathSuffix.endsWith( QLatin1String( "wang_feedback.json" ) ) );
    CHECK( !skips.front().reason.isEmpty() );
    CHECK( csvSkippedRows( tree.outPrefix() + QStringLiteral( ".csv" ), 13 ) == 1 );
}

TEST_CASE( "r10 discovery accounting: a clean class reports zero skips",
           "[teaching_r10][batch]" )
{
    DiscoveryTree tree( { QStringLiteral( "zhang/artifact_1.tif" ),
                          QStringLiteral( "li/artifact_2.tif" ) } );
    const BatchAssessmentReport report = runBatchAssessment(
      discoveryConfig( tree.submissions.path(), tree.checkpointPath() ),
      []( const SubmissionItem &item ) { return passRow( item ); } );

    CHECK( report.total == 2 );
    REQUIRE( publishBatchOutputsAtomic( report, tree.outPrefix() ) );
    const QJsonObject doc = readJson( tree.outPrefix() + QStringLiteral( ".json" ) );
    CHECK( doc.value( QStringLiteral( "skipped" ) ).toInt() == 0 );
    CHECK( doc.value( QStringLiteral( "skipped_files" ) ).toArray().isEmpty() );
    CHECK( csvSkippedRows( tree.outPrefix() + QStringLiteral( ".csv" ), 13 ) == 0 );
}

TEST_CASE( "r10 discovery accounting: in-memory report carries the refusals",
           "[teaching_r10][batch]" )
{
    DiscoveryTree tree( { QStringLiteral( "zhang/artifact_1.tif" ),
                          QStringLiteral( "zhang/zhang_feedback.json" ) } );
    const BatchAssessmentReport report = runBatchAssessment(
      discoveryConfig( tree.submissions.path(), tree.checkpointPath() ),
      []( const SubmissionItem &item ) { return passRow( item ); } );

    CHECK( report.total == 1 );
    CHECK( report.rows.size() == 1 );
    REQUIRE( report.skippedFiles.size() == 1 );
    CHECK( report.skippedFiles.front().studentId == QStringLiteral( "zhang" ) );
    CHECK( report.skippedFiles.front().path.endsWith( QLatin1String( "zhang_feedback.json" ) ) );
    CHECK( report.skippedFiles.front().reason.contains( QLatin1String( "feedback" ) ) );
    CHECK( report.toJson().value( QStringLiteral( "skipped" ) ).toInt() == 1 );
}

TEST_CASE( "r10 discovery accounting: skips never become rows or checkpoint entries",
           "[teaching_r10][batch]" )
{
    DiscoveryTree tree( { QStringLiteral( "zhang/artifact_1.tif" ),
                          QStringLiteral( "zhang/zhang_feedback.json" ),
                          QStringLiteral( "zhang/zhang_summary.json" ) } );
    const QString ckpt = tree.checkpointPath();

    const BatchAssessmentReport first = runBatchAssessment(
      discoveryConfig( tree.submissions.path(), ckpt ),
      []( const SubmissionItem &item ) { return passRow( item ); } );
    CHECK( first.total == 1 );
    CHECK( first.graded == 1 );
    REQUIRE( first.skippedFiles.size() == 2 );

    // The durable checkpoint holds graded rows only — a restart adopts the
    // graded artifact and re-accounts the same refusals; nothing skipped
    // may leak into rows (a fake "graded" row) or the adoption key space.
    const QJsonObject ckptDoc = readJson( ckpt );
    CHECK( ckptDoc.value( QStringLiteral( "rows" ) ).toArray().size() == 1 );
    for ( const auto &v : ckptDoc.value( QStringLiteral( "rows" ) ).toArray() )
        CHECK( v.toObject().value( QStringLiteral( "status" ) ).toString()
               != QLatin1String( "skipped" ) );

    const BatchAssessmentReport second = runBatchAssessment(
      discoveryConfig( tree.submissions.path(), ckpt ),
      []( const SubmissionItem & ) {
          FAIL( "the adopted artifact must not re-grade" );
          return BatchRowResult{};
      } );
    CHECK( second.resumed == 1 );
    CHECK( second.total == 1 );
    CHECK( second.skippedFiles.size() == 2 );
}

TEST_CASE( "r14e discovery containment: symlinked entries are refused, not followed",
           "[teaching_r14e][batch][discovery]" )
{
    // Containment (R14-E audit, teaching-side F1): Qt follows symlinks in
    // QDir listings by default, so a symlinked student folder or a
    // file-level link could grade content from OUTSIDE the submissions
    // root, and a dangling link vanished with no row at all. All three
    // shapes must be typed refusals that never enter the grading set.
    DiscoveryTree tree( { QStringLiteral( "alice/report.json" ) } );

    // External secrets outside the submissions root.
    QFile secret( tree.dir.path() + QStringLiteral( "/answer_key.json" ) );
    REQUIRE( secret.open( QIODevice::WriteOnly ) );
    secret.write( "{}" );
    secret.close();
    QDir outsideBob( tree.dir.path() + QStringLiteral( "/outside_bob" ) );
    REQUIRE( outsideBob.mkpath( QStringLiteral( "." ) ) );
    QFile bobReport( outsideBob.filePath( QStringLiteral( "report.json" ) ) );
    REQUIRE( bobReport.open( QIODevice::WriteOnly ) );
    bobReport.write( "{}" );
    bobReport.close();

    // bob/ is a symlink to a directory outside the submissions root.
    REQUIRE( QFile( outsideBob.path() ).link( tree.submissions.filePath( "bob" ) ) );
    // alice/leak.json is a symlink to an external file.
    REQUIRE( QFile( secret.fileName() )
                 .link( tree.submissions.filePath( QStringLiteral( "alice/leak.json" ) ) ) );
    // carol/dangling.json points at nothing.
    QDir( tree.submissions.path() ).mkpath( QStringLiteral( "carol" ) );
    REQUIRE( QFile( tree.dir.path() + QStringLiteral( "/does_not_exist.json" ) )
                 .link( tree.submissions.filePath(
                   QStringLiteral( "carol/dangling.json" ) ) ) );

    QVector<DiscoverySkip> skips;
    const QVector<SubmissionItem> items =
        discoverSubmissions( tree.submissions.path(), &skips );

    // Only the real file is graded.
    REQUIRE( items.size() == 1 );
    CHECK( items.front().studentId == QStringLiteral( "alice" ) );
    CHECK( items.front().path.endsWith( QLatin1String( "report.json" ) ) );

    // Every symlinked entry is an attributable typed refusal; dangling
    // links are visible too instead of silently vanishing.
    REQUIRE( skips.size() == 3 );
    bool sawLinkedFolder = false, sawLinkedFile = false, sawDangling = false;
    for ( const DiscoverySkip &skip : skips )
    {
        CHECK( !skip.reason.isEmpty() );
        if ( skip.path.endsWith( QLatin1String( "/bob" ) ) )
        {
            sawLinkedFolder = true;
            CHECK( skip.studentId == QStringLiteral( "bob" ) );
        }
        if ( skip.path.endsWith( QLatin1String( "leak.json" ) ) )
        {
            sawLinkedFile = true;
            CHECK( skip.studentId == QStringLiteral( "alice" ) );
        }
        if ( skip.path.endsWith( QLatin1String( "dangling.json" ) ) )
        {
            sawDangling = true;
            CHECK( skip.studentId == QStringLiteral( "carol" ) );
        }
    }
    CHECK( sawLinkedFolder );
    CHECK( sawLinkedFile );
    CHECK( sawDangling );

    // End-to-end honesty: the published report carries the refusals and
    // nothing outside the root is ever graded.
    const BatchAssessmentReport report = runBatchAssessment(
      discoveryConfig( tree.submissions.path(), tree.checkpointPath() ),
      []( const SubmissionItem &item ) {
          CHECK( item.studentId == QStringLiteral( "alice" ) );
          return passRow( item );
      } );
    CHECK( report.total == 1 );
    CHECK( report.skippedFiles.size() == 3 );
}

TEST_CASE( "r14e discovery ids: Unicode normalization variants grade as one student",
           "[teaching_r14e][batch][discovery]" )
{
    // "sān" in NFC (from the teacher's keyboard) and the same name in NFD
    // (from a macOS/zip extractor that decomposes combining marks) must
    // reconcile to ONE student id, or the class counts cannot balance
    // (R14-E audit F4). NOTE: the name must contain a canonically
    // decomposable character — pure CJK like 张三 is normalization-invariant.
    const QString nfc = QStringLiteral( "student-s\u0101n" );
    const QString nfd = nfc.normalized( QString::NormalizationForm_D );
    REQUIRE( nfd != nfc );

    DiscoveryTree tree( { QStringLiteral( "alice/report.json" ) } );
    QDir( tree.submissions.path() ).mkpath( nfd );
    QFile f( tree.submissions.filePath( nfd + QStringLiteral( "/report.json" ) ) );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "payload" );
    f.close();

    QVector<DiscoverySkip> skips;
    const QVector<SubmissionItem> items =
        discoverSubmissions( tree.submissions.path(), &skips );
    REQUIRE( items.size() == 2 );
    bool sawNormalized = false;
    int aliceRows = 0;
    for ( const SubmissionItem &item : items )
    {
        if ( item.path.contains( nfd ) )
        {
            // The NFD-named folder's row must carry the NFC id.
            INFO( "student id: " << item.studentId.toUtf8().constData() );
            CHECK( item.studentId == nfc );
            sawNormalized = true;
        }
        else if ( item.studentId == QLatin1String( "alice" ) )
        {
            ++aliceRows;
        }
    }
    CHECK( sawNormalized );
    CHECK( aliceRows == 1 );
    CHECK( skips.isEmpty() );
}

TEST_CASE( "r14e bounded read: oversize documents are typed refusals, not slurps",
           "[teaching_r14e][json_util]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "doc.json" ) );
    {
        QFile f( path );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( 64, 'x' ) );
    }
    // Within budget: bytes flow.
    const auto ok = sicnu::teaching_admin::readBounded( path, 1024 );
    CHECK( !ok.oversize );
    CHECK( !ok.unreadable );
    CHECK( ok.bytes.size() == 64 );
    // Over budget: refused BEFORE any proportional allocation.
    const auto oversize = sicnu::teaching_admin::readBounded( path, 16 );
    CHECK( oversize.oversize );
    CHECK( !oversize.unreadable );
    CHECK( oversize.bytes.isEmpty() );
    // Missing file: unreadable, not oversize.
    const auto missing =
        sicnu::teaching_admin::readBounded( dir.filePath( QStringLiteral( "nope.json" ) ), 1024 );
    CHECK( missing.unreadable );
    CHECK( !missing.oversize );
}
