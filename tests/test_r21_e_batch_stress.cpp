// tests/test_r21_e_batch_stress.cpp — R21-E W5 (O-E2): course-scale batch
// grading invariants under stress inputs, with wall-time and RSS evidence.
//
// The runner is the production engine (sicnu_lab_batch) with the grading
// callable injected (the same seam the CLI uses); this lane measures, it
// does not gate on timing (a wall-clock pass gate is the #1622 anti-pattern).
//
// Invariants under stress (100 / 500 / 1000 submissions):
//   * row-count: every discovered submission yields exactly one row —
//     duplicates by content digest counted as duplicates, never re-graded
//     into double rows; isolated (throwing) and unavailable (typed refusal)
//     submissions are reported as themselves, never folded into the graded
//     count, never zeroed;
//   * re-run over the same corpus is byte-identical (Truncate CSV: an
//     interrupted run's partial output is healed by a full deterministic
//     re-run — no append, no score accumulation);
//   * peak RSS delta reported as evidence.

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "cli/lab_batch_runner.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>

namespace
{
long currentRssKb()
{
    std::ifstream status( "/proc/self/status" );
    std::string key;
    long value = 0;
    std::string unit;
    while ( status >> key )
    {
        if ( key == "VmHWM:" )
        {
            status >> value >> unit;
            return value;
        }
        status.ignore( 4096, '\n' );
    }
    return 0;
}

void makeSubmission( const QDir &dir, const QString &name, const QByteArray &bytes )
{
    QFile file( dir.filePath( name ) );
    file.open( QIODevice::WriteOnly );
    file.write( bytes );
    file.close();
}

sicnu::agent::OutputVerifier::LabGradeResult gradedResult( double score )
{
    sicnu::agent::OutputVerifier::LabGradeResult result;
    result.graded = true;
    result.score = score;
    result.verdict = score >= 60.0 ? QStringLiteral( "pass" ) : QStringLiteral( "fail" );
    return result;
}

std::string readAll( const QString &path )
{
    QFile file( path );
    file.open( QIODevice::ReadOnly );
    return file.readAll().toStdString();
}

/// Runs the stress at one scale; returns the measured wall milliseconds.
int runAtScale( int submissions, bool withRerun )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    const QDir dir( temp.path() );

    int duplicatePairs = 0;
    for ( int i = 0; i < submissions; ++i )
    {
        // Every 50th pair of submissions is a content duplicate (same bytes):
        // the duplicate is STILL graded (its own artifact) and flagged
        // duplicate_of the first submitter — the runner's identity contract.
        if ( i % 50 == 1 && i + 1 < submissions )
        {
            const QByteArray twin( 24 + ( i % 64 ), static_cast<char>( 'A' + ( i % 26 ) ) );
            makeSubmission( dir, QStringLiteral( "s%1.tif" ).arg( i ), twin );
            makeSubmission( dir, QStringLiteral( "s%1.tif" ).arg( i + 1 ), twin );
            ++duplicatePairs;
            ++i; // the twin was written
            continue;
        }
        QByteArray bytes( 16 + ( i % 32 ), static_cast<char>( 'a' + ( i % 26 ) ) );
        bytes.append( QByteArray::number( i ) ); // unique digest per submission
        makeSubmission( dir, QStringLiteral( "s%1.tif" ).arg( i ), bytes );
    }
    // One throwing anchor: the grader throws → isolated, never aborts.
    makeSubmission( dir, QStringLiteral( "z_throw.tif" ), QByteArray( 8, 'z' ) );
    // One typed-unavailable anchor.
    makeSubmission( dir, QStringLiteral( "z_unavailable.tif" ), QByteArray( 9, 'u' ) );

    QTemporaryDir out;
    REQUIRE( out.isValid() );
    const QString csvPath = out.path() + QStringLiteral( "/grades.csv" );

    const auto grade = []( const QString &artifact ) {
        const QString base = QFileInfo( artifact ).completeBaseName();
        if ( base == QLatin1String( "z_throw" ) )
            throw std::runtime_error( "malformed rules: stress anchor" );
        if ( base == QLatin1String( "z_unavailable" ) )
        {
            // The runner's classification contract: an unverifiable result
            // carries verdict "unavailable" (counted separately from a crash
            // and from a real zero — no score is invented).
            sicnu::agent::OutputVerifier::LabGradeResult unavailable;
            unavailable.graded = false;
            unavailable.verdict = QStringLiteral( "unavailable" );
            unavailable.error = QStringLiteral( "fixture absent offline (stress)" );
            unavailable.errorClass = QStringLiteral( "artifact" );
            return unavailable;
        }
        return gradedResult( base.endsWith( QLatin1String( "3" ) ) ? 12.0 : 88.0 );
    };

    const long rssBefore = currentRssKb();
    const auto started = std::chrono::steady_clock::now();
    sicnu::cli::LabBatchOptions options;
    const sicnu::cli::LabBatchSummary summary =
        sicnu::cli::LabBatchRunner::run( temp.path(), QStringLiteral( "ndvi_basics" ), csvPath,
                                         grade, options );
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started )
                           .count();
    const long rssAfter = currentRssKb();

    INFO( "scale=" << submissions << " total=" << summary.total
                  << " rows=" << summary.rows.size() << " graded=" << summary.graded
                  << " duplicates=" << summary.duplicates << " isolated=" << summary.isolated
                  << " unavailable=" << summary.unavailable
                  << " wall_ms=" << elapsed << " rss_hwm_kb=" << rssAfter
                  << " (before=" << rssBefore << ")" );

    // Row-count invariant: every discovered file yields exactly one row.
    REQUIRE( summary.rows.size() == summary.total );
    // Accounting invariant — the runner's REAL classification contract
    // (src/cli/lab_batch_runner.cpp): a row lands in exactly one of
    //   graded            (result.graded == true; duplicates included, they
    //                      are graded rows flagged duplicate_of),
    //   isolated          (the grade callable threw),
    //   unavailable       (!graded && verdict == "unavailable"),
    //   unverifiable      (!graded && any other verdict — a typed refusal,
    //                      counted in NO summary bucket but still a CSV row).
    // An earlier revision of this lane asserted
    // total == graded + isolated + unavailable, which is false in general:
    // the production grader emits verdict "unverifiable" for artifact-class
    // refusals, and those rows would fall outside the sum (R2 P0-1). The
    // unverifiable count is therefore pinned explicitly below.
    const int unverifiable = static_cast<int>( std::count_if(
      summary.rows.begin(), summary.rows.end(), []( const sicnu::cli::LabBatchRow &row ) {
          return row.verdict == QLatin1String( "unverifiable" );
      } ) );
    REQUIRE( summary.total ==
             summary.graded + summary.isolated + summary.unavailable + unverifiable );
    REQUIRE( summary.duplicates == duplicatePairs );
    REQUIRE( summary.isolated == 1 );
    REQUIRE( summary.unavailable == 1 );
    REQUIRE( unverifiable == 0 ); // this lane's injected grader never emits one

    if ( withRerun )
    {
        const QString csv2 = out.path() + QStringLiteral( "/grades_rerun.csv" );
        sicnu::cli::LabBatchOptions rerunOptions;
        const sicnu::cli::LabBatchSummary rerun = sicnu::cli::LabBatchRunner::run(
            temp.path(), QStringLiteral( "ndvi_basics" ), csv2, grade, rerunOptions );
        REQUIRE( rerun.rows.size() == summary.rows.size() );
        REQUIRE( readAll( csv2 ) == readAll( csvPath ) ); // byte-identical heal
    }
    return static_cast<int>( elapsed );
}
} // namespace

TEST_CASE( "batch stress: 100 submissions keep every invariant", "[r21e][batch_stress]" )
{
    const int elapsed = runAtScale( 100, true );
    CHECK( elapsed >= 0 ); // timing is evidence, not a gate (#1622 discipline)
}

TEST_CASE( "batch stress: 500 submissions keep every invariant", "[r21e][batch_stress]" )
{
    const int elapsed = runAtScale( 500, true );
    CHECK( elapsed >= 0 );
}

TEST_CASE( "batch stress: 1000 submissions keep every invariant", "[r21e][batch_stress]" )
{
    const int elapsed = runAtScale( 1000, false ); // one full pass suffices
    CHECK( elapsed >= 0 );
}
