/***************************************************************************
  tests/test_teaching_admin_grader_cli_budget_r16_c_t06.cpp — R16-C/T06 WP2:
  the grader transcript read must honor the byte budget.

  gradeViaCli() caps captured stdout/stderr (#1466) and forwards
  --max-bytes to the CLI, but read the --out transcript back with an
  unbounded readAll(): a grader writing an oversized transcript (hostile,
  buggy, or a different --max-bytes convention) bypassed the budget
  entirely and its document was parsed and graded verbatim. The transcript
  read now goes through readBounded() with the same budget and an
  oversize transcript is a typed unavailable row — never a grade.

  Determinism: the fake CLI is a POSIX sh script that ignores its input
  artifact and writes a FIXED valid sicnu.lab.grade/1 pass transcript,
  padded to a chosen size with legal leading whitespace. POSIX only — the
  case skips on Windows (a .cmd twin cannot be verified here).
 ***************************************************************************/

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/grader_cli_adapter.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QIODevice>
#include <QTemporaryDir>

using namespace sicnu::teaching_admin;

namespace
{

constexpr const char *kFakeDigest =
  "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

/// Fake grading CLI: writes a valid pass transcript (padded to @p padBytes
/// with legal leading whitespace) to --out AND stdout, exits 0 — the shape
/// the real CLI's transcript authority produces, at a controlled size.
QString writePaddedTranscriptCli( const QString &dir, int padBytes )
{
    const QString path = QDir( dir ).filePath( QStringLiteral( "fake_grader_cli.sh" ) );
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    const QByteArray script = QStringLiteral(
                                "#!/bin/sh\n"
                                "out=\"\"\n"
                                "while [ $# -gt 0 ]; do\n"
                                "  case \"$1\" in\n"
                                "    --out) out=\"$2\"; shift 2 ;;\n"
                                "    *) shift ;;\n"
                                "  esac\n"
                                "done\n"
                                "body='{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"%1\","
                                "\"generated_utc\":\"2026-01-01T00:00:00Z\","
                                "\"report\":{\"verdict\":\"pass\",\"score\":88.5,"
                                "\"deductions\":[]}}'\n"
                                "pad=$(printf '%*s' %2 '')\n"
                                "printf '%s%s' \"$pad\" \"$body\" > \"$out\"\n"
                                "printf '%s%s' \"$pad\" \"$body\"\n"
                                "exit 0\n" )
                                .arg( QString::fromLatin1( kFakeDigest ),
                                      QString::number( padBytes ) )
                                .toUtf8();
    f.write( script );
    f.close();
    REQUIRE( QFile::setPermissions(
      path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
              | QFileDevice::ExeGroup | QFileDevice::ExeOther ) );
    return path;
}

GraderCliConfig budgetConfig( const QString &cli, qint64 maxBytes )
{
    GraderCliConfig cfg;
    cfg.cliPath = cli;
    cfg.labIdOrRulesPath = QStringLiteral( "lab15" );
    cfg.maxBytes = maxBytes;
    cfg.timeoutMs = 60000;
    return cfg;
}

} // namespace

TEST_CASE( "gradeViaCli refuses an over-budget transcript instead of grading it",
           "[teaching_admin][grader][r16_c_t06]" )
{
#ifdef Q_OS_WIN
    SUCCEED( "POSIX-shell fake CLI — not runnable on Windows" );
#else
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // ~16 KiB valid transcript against a 2 KiB budget.
    const QString cli = writePaddedTranscriptCli( dir.path(), 16 * 1024 );
    const GraderCliGrade g = gradeViaCli(
      budgetConfig( cli, 2048 ), QDir( dir.path() ).filePath( QStringLiteral( "a.tif" ) ) );

    // The oversized transcript is a typed refusal, never a parsed grade.
    // (unavailableGrade rows keep the wrapper-default started=false — only
    // the crashed path marks started=true; the refusal is the contract.)
    CHECK( g.status == QLatin1String( "unavailable" ) );
    CHECK( g.verdict == QLatin1String( "unavailable" ) );
    CHECK( g.score < 0.0 );
    CHECK( g.unavailableReason == QLatin1String( "grader_transcript_oversize" ) );
#endif
}

TEST_CASE( "gradeViaCli grades a within-budget transcript normally (control)",
           "[teaching_admin][grader][r16_c_t06]" )
{
#ifdef Q_OS_WIN
    SUCCEED( "POSIX-shell fake CLI — not runnable on Windows" );
#else
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString cli = writePaddedTranscriptCli( dir.path(), 4 * 1024 );
    const GraderCliGrade g = gradeViaCli(
      budgetConfig( cli, 1024 * 1024 ), QDir( dir.path() ).filePath( QStringLiteral( "a.tif" ) ) );

    // Same transcript, within budget: the contract is unchanged — this is
    // the guard against "fixing" the budget by refusing everything.
    CHECK( g.status == QLatin1String( "pass" ) );
    CHECK( g.score == Catch::Approx( 88.5 ) );
    CHECK( g.reportDigest == QString::fromLatin1( kFakeDigest ) );
#endif
}

TEST_CASE( "gradeViaCli still grades from stdout when the CLI writes no --out file (fallback contract)",
           "[teaching_admin][grader][r16_c_t06]" )
{
    // Round-1 review P2 companion: readBounded() reports unreadable for a
    // MISSING transcript; that must keep falling back to the stdout mirror —
    // a CLI that only prints the transcript (never writes --out) is a legal
    // grader shape. Guards the partial-bytes refusal added beside it: only
    // a transcript that EXISTED and changed size mid-read is a typed
    // refusal; the missing-file path stays the documented fallback.
#ifdef Q_OS_WIN
    SUCCEED( "POSIX-shell fake CLI — not runnable on Windows" );
#else
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = QDir( dir.path() ).filePath( QStringLiteral( "stdout_only_cli.sh" ) );
    {
        QFile f( path );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        f.write( QStringLiteral(
                   "#!/bin/sh\n"
                   "body='{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"%1\","
                   "\"generated_utc\":\"2026-01-01T00:00:00Z\","
                   "\"report\":{\"verdict\":\"pass\",\"score\":77.0,"
                   "\"deductions\":[]}}'\n"
                   "printf '%s' \"$body\"\n"
                   "exit 0\n" )
                   .arg( QString::fromLatin1( kFakeDigest ) )
                   .toUtf8() );
    }
    REQUIRE( QFile::setPermissions(
        path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                | QFileDevice::ExeGroup | QFileDevice::ExeOther ) );

    const GraderCliGrade g = gradeViaCli(
        budgetConfig( path, 1024 * 1024 ),
        QDir( dir.path() ).filePath( QStringLiteral( "a.tif" ) ) );
    CHECK( g.status == QLatin1String( "pass" ) );
    CHECK( g.score == Catch::Approx( 77.0 ) );
    CHECK( g.reportDigest == QString::fromLatin1( kFakeDigest ) );
#endif
}
