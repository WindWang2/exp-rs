/***************************************************************************
  tests/test_r25_e_discovery_cancel.cpp — R25-E (CT-C6 + #1695-17c
  residual): cooperative discovery cancellation + the single-walk seam.

  Pins:
    1. A cancelled discovery walk yields NOTHING — no items, no skips. A
       partial list would silently grade a subset while looking complete
       (the #1466 vanish discipline); "not discovered" is the only honest
       cancelled answer.
    2. The uncancelled 3-arg form is byte-equivalent to the legacy 2-arg
       walk (same items, same skip accounting) — the flag is inert unless
       set.
    3. Mid-walk cancellation is observable: a walk over a large tree whose
       flag flips after the walk started comes back EMPTY (asserted on the
       result only, never on wall clock — #1699 discipline).
    4. The CT-C6 seam (BatchAssessmentConfig::preDiscovered) makes the
       orchestrator grade EXACTLY the caller's walk: identical report JSON
       to the internal-discovery run (rows, totals, skips), and — the
       #1694 authority angle — a checkpoint written by one path is ADOPTED
       by the other, proving the seam is transport (outside the config
       digest), not grading authority.
    5. A direct-API run cancelled during ITS internal discovery types the
       report cancelledEarly instead of reading as an empty class.
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <thread>

using namespace sicnu::teaching_admin;

namespace
{

struct ClassTree
{
    QTemporaryDir dir;
    QDir submissions;

    // studentIds each get one artifact file; reserved-named files are added
    // so the skip accounting is exercised on every walk; flatRootFiles adds
    // root-level submissions so the flat pass carries real work too.
    explicit ClassTree( const QStringList &studentIds, const QStringList &reservedFiles = {},
                        int flatRootFiles = 0 )
    {
        submissions = dir.path() + QStringLiteral( "/submissions" );
        submissions.mkpath( QStringLiteral( "." ) );
        int fileNo = 0;
        for ( const QString &student : studentIds )
        {
            QDir sd( submissions.filePath( student ) );
            sd.mkpath( QStringLiteral( "." ) );
            QFile f( sd.filePath( QStringLiteral( "artifact_%1.tif" ).arg( ++fileNo ) ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( "payload" );
        }
        for ( const QString &reserved : reservedFiles )
        {
            QFile f( submissions.filePath( reserved ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( "tool-output" );
        }
        for ( int i = 0; i < flatRootFiles; ++i )
        {
            QFile f( submissions.filePath( QStringLiteral( "flat_%1.tif" ).arg( i, 3, 10, QLatin1Char( '0' ) ) ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( "payload" );
        }
    }

    QString checkpointPath() const { return dir.path() + QStringLiteral( "/ckpt.json" ); }
};

BatchAssessmentConfig baseConfig( const QString &submissionsDir, const QString &checkpointPath )
{
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab25e_discovery" );
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

BatchRowResult passingRow( const SubmissionItem &item )
{
    BatchRowResult row;
    row.studentId = item.studentId;
    row.artifactPath = item.path;
    row.status = QStringLiteral( "pass" );
    row.verdict = QStringLiteral( "pass" );
    row.score = 88.0;
    return row;
}

/// A wide tree (many student dirs AND root-level flat files): a mid-walk
/// flip necessarily lands inside the walk, and BOTH walk passes (student
/// dirs + flat root files) carry real entries.
ClassTree wideTree()
{
    QStringList students;
    for ( int i = 0; i < 4000; ++i )
        students << QStringLiteral( "student_%1" ).arg( i, 4, 10, QLatin1Char( '0' ) );
    return ClassTree{ students, {}, 200 };
}

bool sameItems( const QVector<SubmissionItem> &a, const QVector<SubmissionItem> &b )
{
    if ( a.size() != b.size() )
        return false;
    for ( int i = 0; i < a.size(); ++i )
    {
        if ( a[i].studentId != b[i].studentId || a[i].path != b[i].path ||
             a[i].kind != b[i].kind || a[i].bytes != b[i].bytes )
            return false;
    }
    return true;
}

bool sameSkips( const QVector<DiscoverySkip> &a, const QVector<DiscoverySkip> &b )
{
    if ( a.size() != b.size() )
        return false;
    for ( int i = 0; i < a.size(); ++i )
    {
        if ( a[i].studentId != b[i].studentId || a[i].path != b[i].path ||
             a[i].reason != b[i].reason )
            return false;
    }
    return true;
}

} // namespace

TEST_CASE( "cancelled discovery yields no partial list", "[r25_e][batch][discovery]" )
{
    ClassTree tree{ { QStringLiteral( "alice" ), QStringLiteral( "bob" ) },
                    { QStringLiteral( "grades_summary.json" ) } };

    SECTION( "flag set before the walk" )
    {
        std::atomic<bool> cancel{ true };
        QVector<DiscoverySkip> skips;
        const auto items = discoverSubmissions( tree.submissions.path(), &skips, &cancel );
        REQUIRE( items.isEmpty() );
        REQUIRE( skips.isEmpty() );
    }

    SECTION( "unflagged walk is identical to the legacy form" )
    {
        QVector<DiscoverySkip> legacySkips;
        const auto legacy = discoverSubmissions( tree.submissions.path(), &legacySkips );

        QVector<DiscoverySkip> nullFlagSkips;
        const auto withNullFlag =
          discoverSubmissions( tree.submissions.path(), &nullFlagSkips, nullptr );
        std::atomic<bool> notCancelled{ false };
        QVector<DiscoverySkip> clearFlagSkips;
        const auto withClearFlag =
          discoverSubmissions( tree.submissions.path(), &clearFlagSkips, &notCancelled );

        REQUIRE( sameItems( legacy, withNullFlag ) );
        REQUIRE( sameItems( legacy, withClearFlag ) );
        REQUIRE( sameSkips( legacySkips, nullFlagSkips ) );
        REQUIRE( sameSkips( legacySkips, clearFlagSkips ) );
        // The tree's shape: two submissions discovered, the reserved output
        // refused and accounted.
        REQUIRE( legacy.size() == 2 );
        REQUIRE( legacySkips.size() == 1 );
    }
}

TEST_CASE( "mid-walk cancellation aborts the discovery walk", "[r25_e][batch][discovery]" )
{
    SECTION( "flip while the walk is running (both passes carry entries)" )
    {
        ClassTree tree = wideTree();

        std::atomic<bool> cancel{ false };
        // Flip the flag after the walk has certainly started. The sleep is
        // deliberately SHORT (1 ms): the assertion holds whenever the flip
        // lands at or after the walk's start (cancelled ⇒ empty), and a
        // long sleep is the racy direction — the walk could finish first
        // and a completed walk is legitimately non-empty.
        std::thread watcher( [ &cancel ]() {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            cancel.store( true, std::memory_order_relaxed );
        } );
        QVector<DiscoverySkip> skips;
        const auto items = discoverSubmissions( tree.submissions.path(), &skips, &cancel );
        watcher.join();

        REQUIRE( items.isEmpty() );
        REQUIRE( skips.isEmpty() );
    }

    SECTION( "flip before the walk starts is deterministically empty" )
    {
        ClassTree tree = wideTree();
        std::atomic<bool> cancel{ true };
        QVector<DiscoverySkip> skips;
        const auto items = discoverSubmissions( tree.submissions.path(), &skips, &cancel );
        REQUIRE( items.isEmpty() );
        REQUIRE( skips.isEmpty() );
    }
}

TEST_CASE( "preDiscovered seam grades exactly the caller's walk (CT-C6)",
           "[r25_e][batch][discovery]" )
{
    // Reserved tool-output names only refuse INSIDE a student directory (a
    // root-level file is the flat pass's business — master semantics), so
    // the accounted refusal lives in alice's folder here.
    ClassTree tree{ { QStringLiteral( "alice" ), QStringLiteral( "bob" ),
                      QStringLiteral( "cara" ) } };
    {
        QFile f( tree.submissions.filePath( QStringLiteral( "alice/alice_feedback.json" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "tool-output" );
    }
    const QString ckpt = tree.checkpointPath();

    // Run A: internal discovery (the seam is empty — the legacy path).
    BatchAssessmentConfig cfgA = baseConfig( tree.submissions.path(), ckpt );
    const BatchAssessmentReport reportA =
      runBatchAssessment( cfgA, []( const SubmissionItem &item ) { return passingRow( item ); } );
    REQUIRE( reportA.total == 3 );
    REQUIRE( reportA.graded == 3 );
    REQUIRE( reportA.skippedFiles.size() == 1 );

    // Run B: the SAME directory walked by the caller and handed in through
    // the seam. The report must be byte-identical (modulo nothing) — same
    // rows in the same discovery order, same totals, same skip accounting.
    QVector<DiscoverySkip> walkSkips;
    const auto walked = discoverSubmissions( tree.submissions.path(), &walkSkips );
    REQUIRE( walked.size() == 3 );

    BatchAssessmentConfig cfgB = baseConfig( tree.submissions.path(), tree.checkpointPath() );
    cfgB.preDiscovered = walked;
    cfgB.preDiscoveredSkips = walkSkips;
    const BatchAssessmentReport reportB =
      runBatchAssessment( cfgB, []( const SubmissionItem &item ) { return passingRow( item ); } );

    const QByteArray jsonA = QJsonDocument( reportA.toJson() ).toJson( QJsonDocument::Compact );
    const QByteArray jsonB = QJsonDocument( reportB.toJson() ).toJson( QJsonDocument::Compact );
    // The report-level `resumed` counter is the one member that legitimately
    // differs between an uninterrupted run and a resumed one (R18-E D3: the
    // ROW documents stay byte-stable; the counter is the report's own
    // provenance). Strip it and demand byte equality of everything else.
    QJsonObject docA = reportA.toJson();
    QJsonObject docB = reportB.toJson();
    docA.remove( QStringLiteral( "resumed" ) );
    docB.remove( QStringLiteral( "resumed" ) );
    REQUIRE( QJsonDocument( docA ).toJson( QJsonDocument::Compact ) ==
             QJsonDocument( docB ).toJson( QJsonDocument::Compact ) );
    REQUIRE( jsonA != jsonB ); // …and the counter itself did move (3 adopted).

    // #1694 authority angle: the seam must NOT be part of the config digest
    // — run B adopts run A's checkpoint rows (same directory, same rules),
    // which only happens when the digest is seam-independent.
    REQUIRE( reportB.resumed == 3 );
    REQUIRE( reportB.graded == 3 );

    // And the adopted rows publish identical bytes to a fresh grade of the
    // same content (restart-identity, R18-E D3): re-serialized row documents
    // are byte-stable across adoption.
    for ( const auto &row : reportB.rows )
        REQUIRE( row.resumed );
}

TEST_CASE( "direct-API run cancelled during internal discovery is typed cancelled",
           "[r25_e][batch][discovery]" )
{
    ClassTree tree{ { QStringLiteral( "alice" ), QStringLiteral( "bob" ) } };
    std::atomic<bool> cancel{ true };
    const auto report = runBatchAssessment(
      baseConfig( tree.submissions.path(), QString() ),
      []( const SubmissionItem &item ) { return passingRow( item ); }, &cancel );
    REQUIRE( report.total == 0 );
    REQUIRE( report.rows.isEmpty() );
    // 0-total on a cancelled run must be attributable, never readable as
    // "the class submitted nothing".
    REQUIRE( report.cancelledEarly );

    // Control: the same tree without the flag is a normal 2-student run.
    std::atomic<bool> noCancel{ false };
    const auto control = runBatchAssessment(
      baseConfig( tree.submissions.path(), QString() ),
      []( const SubmissionItem &item ) { return passingRow( item ); }, &noCancel );
    REQUIRE( control.total == 2 );
    REQUIRE( !control.cancelledEarly );
}

TEST_CASE( "cancel during internal discovery preserves the prior checkpoint (R1 finding 2)",
           "[r25_e][batch][discovery][checkpoint]" )
{
    ClassTree tree{ { QStringLiteral( "alice" ), QStringLiteral( "bob" ) } };
    const auto grader = []( const SubmissionItem &item ) { return passingRow( item ); };

    // Run A completes and durably checkpoints both rows.
    {
        const auto first = runBatchAssessment( baseConfig( tree.submissions.path(),
                                                            tree.checkpointPath() ),
                                               grader );
        REQUIRE( first.graded == 2 );
    }

    // Run B, same config, cancelled during ITS internal discovery: nothing
    // was graded, so nothing may be published — and the durable rows from
    // run A must survive (rewriting {total:0, rows:[]} would destroy the
    // class's resume state).
    {
        std::atomic<bool> cancel{ true };
        const auto cancelled = runBatchAssessment(
          baseConfig( tree.submissions.path(), tree.checkpointPath() ), grader, &cancel );
        REQUIRE( cancelled.total == 0 );
        REQUIRE( cancelled.cancelledEarly );
    }

    // Run C proves it: same config, no cancel — BOTH rows are adopted from
    // the checkpoint run A wrote, not re-graded.
    {
        const auto resumed = runBatchAssessment(
          baseConfig( tree.submissions.path(), tree.checkpointPath() ), grader );
        REQUIRE( resumed.resumed == 2 );
        REQUIRE( resumed.total == 2 );
    }
}

TEST_CASE( "an empty-but-uncancelled class is not typed cancelled (R1 finding 8)",
           "[r25_e][batch][discovery]" )
{
    QTemporaryDir dir;
    const QDir submissions = dir.path() + QStringLiteral( "/empty" );
    submissions.mkpath( QStringLiteral( "." ) );

    const auto report = runBatchAssessment(
      baseConfig( submissions.path(), QString() ),
      []( const SubmissionItem &item ) { return passingRow( item ); } );
    REQUIRE( report.total == 0 );
    // 0-total is reserved for a genuinely empty discovery; the cancelled
    // typing is only for runs whose flag actually flipped.
    REQUIRE( !report.cancelledEarly );
}
