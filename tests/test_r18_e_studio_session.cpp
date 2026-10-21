/***************************************************************************
  tests/test_r18_e_studio_session.cpp — R18-E E02 studio session reader pin.

  T202 (H3): StudioSessionState::fromJson ignored the schema marker, so a
  foreign or future session document imported as if it were current — the
  reader must fail closed on a non-matching schema (the same rule the
  sibling export reader in studio_export.cpp already enforces).
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "experiment_studio/studio_session.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace sicnu::experiment_studio;

namespace
{

StudioSessionState sampleState()
{
    StudioSessionState s;
    s.studyId = QStringLiteral( "study-7" );
    s.experimentId = QStringLiteral( "exp-1" );
    s.algorithmId = QStringLiteral( "rs:ndvi" );
    s.activeTab = QStringLiteral( "matrix" );
    s.selectedPointIds = QStringList{ QStringLiteral( "p1" ), QStringLiteral( "p2" ) };
    s.referenceRunId = QStringLiteral( "run-a" );
    s.studentRunId = QStringLiteral( "run-b" );
    s.faultScenarioId = QStringLiteral( "fault-1" );
    s.lastExportPath = QStringLiteral( "/tmp/export.json" );
    return s;
}

} // namespace

TEST_CASE( "r18e T202: a session with the current schema round-trips and is marked ok",
           "[studio][session][r18_e]" )
{
    const StudioSessionState original = sampleState();
    const StudioSessionState restored = StudioSessionState::fromJson( original.toJson() );
    CHECK( restored.ok );
    CHECK( restored.studyId == original.studyId );
    CHECK( restored.experimentId == original.experimentId );
    CHECK( restored.algorithmId == original.algorithmId );
    CHECK( restored.activeTab == original.activeTab );
    CHECK( restored.selectedPointIds == original.selectedPointIds );
    CHECK( restored.referenceRunId == original.referenceRunId );
    CHECK( restored.studentRunId == original.studentRunId );
    CHECK( restored.faultScenarioId == original.faultScenarioId );
    CHECK( restored.lastExportPath == original.lastExportPath );
}

TEST_CASE( "r18e T202b: a foreign schema marker is refused, never imported",
           "[studio][session][r18_e]" )
{
    QJsonObject doc = sampleState().toJson();
    doc.insert( QStringLiteral( "schema" ), QStringLiteral( "sicnu.experiment_studio.session/2" ) );
    const StudioSessionState restored = StudioSessionState::fromJson( doc );
    CHECK_FALSE( restored.ok );
    // A refused document imports nothing — not even the fields that happen
    // to parse. Otherwise a future reader would silently adopt stale data.
    CHECK( restored.studyId.isEmpty() );
    CHECK( restored.selectedPointIds.isEmpty() );

    QJsonObject missing = sampleState().toJson();
    missing.remove( QStringLiteral( "schema" ) );
    CHECK_FALSE( StudioSessionState::fromJson( missing ).ok );

    QJsonObject foreign = sampleState().toJson();
    foreign.insert( QStringLiteral( "schema" ), QStringLiteral( "some.other.doc/1" ) );
    CHECK_FALSE( StudioSessionState::fromJson( foreign ).ok );
}
