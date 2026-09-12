// src/processing/framework/execution_id.h
#pragma once

#include <QString>

#include <optional>

namespace sicnu::processing {

/// The frozen wire identity of a Task Center execution: "task-<taskId>".
/// The only string form that may cross the MCP seam or be persisted
/// (provenance taskReference, docs/agent protocol examples). The format is
/// byte-frozen — changing it breaks persisted provenance and external MCP
/// clients. Parsing and formatting of the form live only here; callers
/// never build "task-" strings by hand (CONTEXT.md, "Execution Id").
///
/// The Task Center's internal engine jobId ("task-<id>-<uuid8>", minted per
/// dispatch so retries stay distinct) is a JobEngine correlation key, not
/// an execution id: fromWire() rejects it.
class ExecutionId {
public:
  /// Wraps a Task Center task id. Precondition: taskId > 0 (non-positive
  /// ids are failure sentinels and never have a wire form).
  static ExecutionId fromTaskId( long taskId )
  {
    Q_ASSERT( taskId > 0 );
    return ExecutionId( taskId );
  }

  /// Parses "task-<taskId>" with the historical semantics: the prefix is
  /// required, the remainder must be a positive decimal. Returns
  /// std::nullopt for malformed ids — including the engine jobId's
  /// uuid-suffixed form, which is not an execution id.
  static std::optional<ExecutionId> fromWire( const QString &wire )
  {
    if ( !wire.startsWith( QStringLiteral( "task-" ) ) )
      return std::nullopt;
    bool ok = false;
    const long id = wire.mid( 5 ).toLong( &ok );
    if ( !ok || id <= 0 )
      return std::nullopt;
    return ExecutionId( id );
  }

  /// The wire form ("task-42"). Always canonical: never leading zeros,
  /// never a uuid suffix.
  QString toWire() const { return QStringLiteral( "task-%1" ).arg( m_taskId ); }

  /// The Task Center task id this identity wraps.
  long taskId() const { return m_taskId; }

private:
  explicit ExecutionId( long taskId ) : m_taskId( taskId ) {}

  long m_taskId = 0;
};

inline bool operator==( const ExecutionId &a, const ExecutionId &b )
{
  return a.taskId() == b.taskId();
}

inline bool operator!=( const ExecutionId &a, const ExecutionId &b )
{
  return !( a == b );
}

} // namespace sicnu::processing
