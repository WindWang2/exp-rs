// test_r15_E_progress_text.cpp — R15-E / E05 oracle (light lane).
//
// Reproduced on master 46791bc65c6f552ebbcf723bb91892736d56f319:
// RsJobPanel::formatProgress() printed the last known percentage for ANY
// status, so a task that died at 50% rendered "Failed | 50%" — a number that
// reads as partial progress the user may still wait on.
//
// The rule now lives in a pure inline function (src/app/shell/
// task_progress_text.h) so it can be asserted without linking the desktop
// shell; rs_job_panel.cpp delegates to it.
#include <catch2/catch_test_macros.hpp>

#include "app/shell/task_progress_text.h"

using sicnu::TaskStatus;
using sicnu::app::taskprogress::columnText;
using sicnu::app::taskprogress::observedText;

TEST_CASE( "progress column never shows a percentage for a failed or cancelled task",
           "[r15e][e05][progress]" )
{
    CHECK( columnText( 0.5, TaskStatus::Failed ) == QStringLiteral( "—" ) );
    CHECK( columnText( 1.0, TaskStatus::Canceled ) == QStringLiteral( "—" ) );
    CHECK( columnText( 0.0, TaskStatus::Failed ) == QStringLiteral( "—" ) );
}

TEST_CASE( "progress column keeps real percentages for in-flight and completed tasks",
           "[r15e][e05][progress]" )
{
    CHECK( columnText( 0.5, TaskStatus::Running ) == QStringLiteral( "50%" ) );
    CHECK( columnText( 0.5, TaskStatus::Completed ) == QStringLiteral( "50%" ) );
    CHECK( columnText( 1.0, TaskStatus::Running ) == QStringLiteral( "100%" ) );
    CHECK( columnText( 0.0, TaskStatus::Queued ) == QStringLiteral( "0%" ) );
}

TEST_CASE( "progress column clamps out-of-range values and marks unknown ones",
           "[r15e][e05][progress]" )
{
    CHECK( columnText( -1.0, TaskStatus::Running ) == QStringLiteral( "…" ) );
    CHECK( columnText( 4.0, TaskStatus::Running ) == QStringLiteral( "100%" ) );
    // Non-finite values must not produce a nonsense label either.
    CHECK( columnText( 0.335, TaskStatus::Running ) == QStringLiteral( "34%" ) );
}

TEST_CASE( "the detail view keeps reporting how far a stopped task got",
           "[r15e][e05][progress]" )
{
    // The list/column rule refuses a percentage for a stopped task, but the
    // detail panel sits next to an explicit "Status: Failed" line — dropping
    // the observed value there would be a real information loss.
    CHECK( observedText( 0.5 ) == QStringLiteral( "50%" ) );
    CHECK( observedText( -1.0 ) == QStringLiteral( "…" ) );
    CHECK( observedText( 2.0 ) == QStringLiteral( "100%" ) );
}
