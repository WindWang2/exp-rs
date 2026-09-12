// The ExecutionId interface is the test surface: the wire form "task-<id>"
// is byte-frozen (persisted provenance, docs/agent protocol examples), so
// golden strings pin the format itself, not just round-trips. The Task
// Center's engine jobId ("task-<id>-<uuid8>") is a JobEngine correlation
// key, not an execution id — fromWire must keep rejecting it (CONTEXT.md,
// "Execution Id").

#include <catch2/catch_test_macros.hpp>

#include "processing/framework/execution_id.h"

#include <QString>
#include <QStringList>

using sicnu::processing::ExecutionId;

TEST_CASE( "ExecutionId golden wire form is frozen", "[execution_id]" )
{
  CHECK( ExecutionId::fromTaskId( 42 ).toWire() == QStringLiteral( "task-42" ) );
  CHECK( ExecutionId::fromTaskId( 1 ).toWire() == QStringLiteral( "task-1" ) );
  CHECK( ExecutionId::fromTaskId( 1234567 ).toWire() == QStringLiteral( "task-1234567" ) );
}

TEST_CASE( "ExecutionId round-trips", "[execution_id]" )
{
  for ( const long id : { 1L, 7L, 42L, 999999L } )
  {
    const auto parsed = ExecutionId::fromWire( ExecutionId::fromTaskId( id ).toWire() );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->taskId() == id );
    CHECK( *parsed == ExecutionId::fromTaskId( id ) );
  }
}

TEST_CASE( "ExecutionId rejects malformed wire forms", "[execution_id]" )
{
  const QStringList bad = {
    QString(),
    QStringLiteral( "task-" ),
    QStringLiteral( "task" ),
    QStringLiteral( "task-0" ),
    QStringLiteral( "task--1" ),
    QStringLiteral( "task-abc" ),
    QStringLiteral( "not-a-task-id" ),
    QStringLiteral( "TASK-42" ),
    QStringLiteral( "task-1.5" ),
    // The engine jobId (uuid-suffixed) is a JobEngine correlation key.
    QStringLiteral( "task-42-ab12cd34" ),
  };
  for ( const QString &wire : bad )
    CHECK_FALSE( ExecutionId::fromWire( wire ).has_value() );
}

TEST_CASE( "ExecutionId accepts the lenient historical forms", "[execution_id]" )
{
  // Historical parse semantics: toLong is strtol-shaped — leading
  // whitespace is skipped ("task- 42" -> 42) and leading zeros parse
  // ("task-007" -> 7); trailing garbage is rejected ("task-1.5" stays in
  // the malformed list). toWire() normalizes on the way out (hand-built
  // forms were never canonical).
  const auto leadingSpace = ExecutionId::fromWire( QStringLiteral( "task- 42" ) );
  REQUIRE( leadingSpace.has_value() );
  CHECK( leadingSpace->taskId() == 42 );

  const auto leadingZeros = ExecutionId::fromWire( QStringLiteral( "task-007" ) );
  REQUIRE( leadingZeros.has_value() );
  CHECK( leadingZeros->taskId() == 7 );
  CHECK( leadingZeros->toWire() == QStringLiteral( "task-7" ) );
}
