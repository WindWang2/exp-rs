/***************************************************************************
  tests/test_r15_E_batch_refusal_accounting.cpp — R15-E / E08+E09 oracle.

  RESTORED by R20-05 (merge-loss recovery): this file was dropped wholesale by
  merge c8f5e0805422 (r16-b merging PR #1517's r15-E side). The discovery
  accounting half was independently re-delivered by the #1466 tail fix
  (0a8f56a28d, R10-B B6) and stays green on master — kept as the protective
  regression for that invariant. The fail-closed published-report loader half
  (`loadBatchReportFromFile`) never came back: the function did not exist on
  master 28e1891390 and the teacher console still re-read the report with
  bare `doc.value("total").toInt()` calls (teaching_admin_dock.cpp:558/618),
  so a truncated / half-synced / unrelated JSON file produced a structurally
  valid "0 submissions / 0% completion" class summary. R20-05 restores the
  loader and the two console call sites; the loader cases below are RED
  against the pre-restoration tree at compile time (API absent) and pin the
  fail-closed contract from here on.

  Original reproduction context (r15-E, master 46791bc65c):

   1. discoverSubmissions() dropped HIDDEN entries and NESTED directories with
      a bare `continue`. A student folder carrying the hidden attribute
      (U-disk copies, some unzip tools) or a lesson-grouped layout
      (`submissions/2021级/张三/hw.tif`) vanished from rows, skipped_files,
      report.total AND the checkpoint at the same time — the console reported
      "graded 5 / 5" for a class of six. batch_assessment.h states the
      opposite invariant: "Refusals are deliberate … but they are ACCOUNTED,
      never a silent vanish".

   2. The teacher console re-read the published batch JSON with bare
      `doc.value("total").toInt()` calls, so a truncated / half-synced /
      unrelated JSON file produced a structurally valid
      "0 submissions / 0% completion" class summary: a read failure rendered
      as "this class really has no homework".
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#if defined( Q_OS_WIN )
#  include <qt_windows.h>
#endif

using namespace sicnu::teaching_admin;

namespace
{

/// Makes @p path hidden the way the platform actually means it: a dot prefix
/// on Unix, the FILE_ATTRIBUTE_HIDDEN bit on Windows (matching what a U-disk
/// copy or an unzip tool leaves behind — the real-world trigger).
void makeHidden( const QString &path )
{
#if defined( Q_OS_WIN )
    SetFileAttributesW( reinterpret_cast<const wchar_t *>( path.utf16() ),
                        FILE_ATTRIBUTE_HIDDEN );
#else
    Q_UNUSED( path ); // dot-prefixed names are already hidden
#endif
}

QString writeFile( const QDir &dir, const QString &rel )
{
    const QFileInfo fi( dir.filePath( rel ) );
    QDir().mkpath( fi.absolutePath() );
    QFile f( fi.absoluteFilePath() );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "payload" );
    f.close();
    return fi.absoluteFilePath();
}

QStringList skipPaths( const QVector<DiscoverySkip> &skips )
{
    QStringList out;
    for ( const DiscoverySkip &s : skips )
        out << s.path;
    return out;
}

QString skipReason( const QVector<DiscoverySkip> &skips, const QString &path )
{
    for ( const DiscoverySkip &s : skips )
    {
        if ( s.path == path )
            return s.reason;
    }
    return QString();
}

/// How many times @p path was booked. Must be exactly one: the hidden-entry
/// rule exists in two passes (student directories and root-level flat files),
/// and a path caught by both would inflate the refusal count.
int skipCount( const QVector<DiscoverySkip> &skips, const QString &path )
{
    int n = 0;
    for ( const DiscoverySkip &s : skips )
    {
        if ( s.path == path )
            ++n;
    }
    return n;
}

QVector<DiscoverySkip> skipsFor( const QString &submissionsDir )
{
    QVector<DiscoverySkip> skips;
    discoverSubmissions( submissionsDir, &skips );
    return skips;
}

} // namespace

TEST_CASE( "a hidden student directory is a typed refusal, not a silent vanish",
           "[r15e][e08][discovery]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    const QDir submissions( temp.path() + QStringLiteral( "/submissions" ) );
    QDir().mkpath( submissions.path() );

    // Visible student, graded as usual.
    writeFile( submissions, QStringLiteral( "李四/hw.tif" ) );
    // Hidden student — the whole submission.
    const QString hiddenStudentDir = submissions.filePath( QStringLiteral( ".张三" ) );
    QDir().mkpath( hiddenStudentDir );
    const QString hiddenArtifact = writeFile( QDir( hiddenStudentDir ), QStringLiteral( "hw.tif" ) );
    makeHidden( hiddenStudentDir );

    QVector<DiscoverySkip> skips;
    const QVector<SubmissionItem> items = discoverSubmissions( submissions.path(), &skips );

    REQUIRE( items.size() == 1 );
    CHECK( items.front().studentId == QStringLiteral( "李四" ) );

    // The hidden student must be ACCOUNTED, with a reason that says why.
    const QStringList refused = skipPaths( skips );
    CHECK( refused.contains( hiddenStudentDir ) );
    CHECK( skipReason( skips, hiddenStudentDir ).contains( QStringLiteral( "hidden" ) ) );
    // ... and it must be attributable to the directory the teacher has to
    // look at, so they can find the work. studentId is the directory name
    // verbatim — including the leading dot, because that IS the folder name
    // on disk; a stripped name would not match anything in the file manager.
    bool attributed = false;
    for ( const DiscoverySkip &s : skips )
    {
        if ( s.path == hiddenStudentDir
             && s.studentId == QString::fromUtf8( ".\u5f20\u4e09" ) )
            attributed = true;
    }
    CHECK( attributed );
    // Exactly once: the root-flat pass must not book the same directory again.
    CHECK( skipCount( skips, hiddenStudentDir ) == 1 );
    Q_UNUSED( hiddenArtifact );
}

TEST_CASE( "a hidden root-level file is accounted exactly once, unattributed",
           "[r15e][e08][discovery]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    const QDir submissions( temp.path() + QStringLiteral( "/submissions" ) );
    QDir().mkpath( submissions.path() );

    writeFile( submissions, QStringLiteral( "李四/hw.tif" ) );
    const QString hiddenRootFile = writeFile( submissions, QStringLiteral( ".leaked_answer.tif" ) );
    makeHidden( hiddenRootFile );

    const QVector<DiscoverySkip> skips = skipsFor( submissions.path() );
    CHECK( skipCount( skips, hiddenRootFile ) == 1 );
    for ( const DiscoverySkip &s : skips )
    {
        if ( s.path == hiddenRootFile )
        {
            // A root-level file has no owning student directory; it must not
            // borrow its own file name as a studentId.
            CHECK( s.studentId.isEmpty() );
            CHECK( s.reason.contains( QStringLiteral( "hidden" ) ) );
        }
    }
}

TEST_CASE( "a hidden submission file inside a student directory is accounted",
           "[r15e][e08][discovery]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    const QDir submissions( temp.path() + QStringLiteral( "/submissions" ) );
    QDir().mkpath( submissions.path() );

    writeFile( submissions, QStringLiteral( "王五/hw.tif" ) );
    const QString hiddenFile = writeFile( submissions, QStringLiteral( "王五/.hw.tif" ) );
    makeHidden( hiddenFile );

    QVector<DiscoverySkip> skips;
    const QVector<SubmissionItem> items = discoverSubmissions( submissions.path(), &skips );

    REQUIRE( items.size() == 1 );
    CHECK( items.front().path.endsWith( QStringLiteral( "hw.tif" ) ) );
    CHECK( skipPaths( skips ).contains( hiddenFile ) );
    CHECK( skipReason( skips, hiddenFile ).contains( QStringLiteral( "hidden" ) ) );
}

TEST_CASE( "a nested directory below a student folder is accounted",
           "[r15e][e08][discovery]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    const QDir submissions( temp.path() + QStringLiteral( "/submissions" ) );
    QDir().mkpath( submissions.path() );

    writeFile( submissions, QStringLiteral( "赵六/hw.tif" ) );
    // Lesson-grouped layout: the class folder nests one level too deep.
    const QFileInfo nested( submissions.filePath( QStringLiteral( "2021级遥感1班/孙七/hw.tif" ) ) );
    QDir().mkpath( nested.absolutePath() );
    QFile f( nested.absoluteFilePath() );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "payload" );
    f.close();

    QVector<DiscoverySkip> skips;
    const QVector<SubmissionItem> items = discoverSubmissions( submissions.path(), &skips );

    // The nested student is not graded (one-level contract)…
    REQUIRE( items.size() == 1 );
    CHECK( items.front().studentId == QStringLiteral( "赵六" ) );
    // …but the directory that swallowed the whole lesson IS accounted for,
    // so "0 submissions" is never reported for a populated folder.
    const QString nestedDir = submissions.filePath( QStringLiteral( "2021级遥感1班/孙七" ) );
    CHECK( skipPaths( skips ).contains( nestedDir ) );
    CHECK( skipReason( skips, nestedDir ).contains( QStringLiteral( "nested" ) ) );

    // The run-level reconciliation the invariant promises.
    BatchAssessmentReport report;
    discoverSubmissions( submissions.path(), &report.skippedFiles );
    CHECK( report.skippedFiles.size() >= 1 );
}

// ---------------------------------------------------------------------------
// Published-report loading must fail closed.
// ---------------------------------------------------------------------------

TEST_CASE( "a truncated batch report is refused, never summarized as empty",
           "[r15e][e08][report]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    const QString path = temp.filePath( QStringLiteral( "batch.json" ) );
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "{\"schema\":\"sicnu.teaching.batch-orchestration/1\",\"lab_id\":\"lab01\","
             "\"rows\":[{\"student_id\":\"zhang\"" );
    f.close();

    BatchAssessmentReport report;
    QString error;
    CHECK_FALSE( loadBatchReportFromFile( path, report, error ) );
    CHECK_FALSE( error.isEmpty() );
    CHECK( report.total == 0 );
    CHECK( report.rows.isEmpty() );
}

TEST_CASE( "a non-batch JSON document is refused, never summarized as empty",
           "[r15e][e08][report]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    const QString path = temp.filePath( QStringLiteral( "other.json" ) );
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "{\"schema\":\"something.else/1\",\"value\":42}" );
    f.close();

    BatchAssessmentReport report;
    QString error;
    CHECK_FALSE( loadBatchReportFromFile( path, report, error ) );
    CHECK( error.contains( QStringLiteral( "rows" ) ) );
}

TEST_CASE( "a missing batch report is refused", "[r15e][e08][report]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    BatchAssessmentReport report;
    QString error;
    CHECK_FALSE( loadBatchReportFromFile( temp.filePath( QStringLiteral( "nope.json" ) ),
                                          report, error ) );
    CHECK_FALSE( error.isEmpty() );
}

TEST_CASE( "a batch report with a non-object row entry is refused",
           "[r15e][e08][report]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    const QString path = temp.filePath( QStringLiteral( "rows.json" ) );
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "{\"lab_id\":\"lab01\",\"total\":2,"
             "\"rows\":[{\"student_id\":\"a\",\"status\":\"pass\"},42]}" );
    f.close();

    BatchAssessmentReport report;
    QString error;
    // Silently dropping the bad row would understate the class exactly like
    // the empty-summary bug this loader exists to prevent.
    CHECK_FALSE( loadBatchReportFromFile( path, report, error ) );
    CHECK_FALSE( error.isEmpty() );
}

TEST_CASE( "a valid batch report round-trips and reports a total mismatch",
           "[r15e][e08][report]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );

    BatchAssessmentReport written;
    written.labId = QStringLiteral( "lab01" );
    written.total = 3;
    written.rubricVersion = QStringLiteral( "r1" );
    for ( int i = 0; i < 3; ++i )
    {
        BatchRowResult row;
        row.studentId = QStringLiteral( "s%1" ).arg( i );
        row.labId = written.labId;
        row.status = QStringLiteral( "pass" );
        row.score = 80.0 + i;
        written.rows.push_back( row );
    }
    const QString prefix = temp.filePath( QStringLiteral( "report" ) );
    REQUIRE( publishBatchOutputsAtomic( written, prefix ) );

    BatchAssessmentReport loaded;
    QString error;
    QString warning;
    REQUIRE( loadBatchReportFromFile( prefix + QStringLiteral( ".json" ), loaded, error, &warning ) );
    CHECK( error.isEmpty() );
    CHECK( warning.isEmpty() );
    CHECK( loaded.labId == written.labId );
    CHECK( loaded.total == 3 );
    REQUIRE( loaded.rows.size() == 3 );
    CHECK( loaded.rows.front().studentId == QStringLiteral( "s0" ) );

    // A declared total that disagrees with the rows is surfaced, not silently
    // replaced (the console must be able to say so).
    QFile rf( prefix + QStringLiteral( ".json" ) );
    REQUIRE( rf.open( QIODevice::ReadOnly ) );
    QJsonObject doc = QJsonDocument::fromJson( rf.readAll() ).object();
    rf.close();
    doc[QStringLiteral( "total" )] = 0;
    QFile wf( prefix + QStringLiteral( ".json" ) );
    REQUIRE( wf.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    wf.write( QJsonDocument( doc ).toJson( QJsonDocument::Compact ) );
    wf.close();

    BatchAssessmentReport again;
    QString secondWarning;
    REQUIRE( loadBatchReportFromFile( prefix + QStringLiteral( ".json" ), again, error,
                                      &secondWarning ) );
    CHECK( again.total == 0 );
    CHECK( again.rows.size() == 3 );
    CHECK_FALSE( secondWarning.isEmpty() );
}
