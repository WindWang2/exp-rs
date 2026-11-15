#include "conversation_metrics.h"

#include "runtime/observability/trace.h"

#include <QJsonDocument>
#include <QStringList>

#include <cmath>

namespace sicnu::agent::runtime
{
namespace
{
using sicnu::runtime::observability::trace::Trace;
using sicnu::runtime::observability::trace::TraceEvent;
constexpr double kMaxSafeInteger = 9007199254740991.0;
const QStringList kUsageFields{ QStringLiteral( "input" ), QStringLiteral( "output" ),
  QStringLiteral( "cacheRead" ), QStringLiteral( "cacheWrite" ), QStringLiteral( "totalTokens" ) };

QJsonObject zeroUsage()
{
  QJsonObject result;
  for ( const auto &key : kUsageFields ) result[key] = 0;
  return result;
}
}

ConversationMetrics::ConversationMetrics( AgentConversationRuntime *runtime, Backend backend )
  : QObject( runtime ), m_backend( backend )
{
  connect( runtime, &AgentConversationRuntime::runStarted, this, &ConversationMetrics::begin );
  connect( runtime, &AgentConversationRuntime::modelRequestStarted, this, &ConversationMetrics::request );
  connect( runtime, &AgentConversationRuntime::usageUpdated, this, &ConversationMetrics::usage );
  connect( runtime, &AgentConversationRuntime::contentDelta, this,
    [this]( const QString &run, const QString &text ) { if ( !text.isEmpty() ) firstDelta( run ); } );
  connect( runtime, &AgentConversationRuntime::reasoningDelta, this,
    [this]( const QString &run, const QString &text ) { if ( !text.isEmpty() ) firstDelta( run ); } );
  connect( runtime, &AgentConversationRuntime::settled, this,
    [this]( const QString &run, bool aborted, const QString &error ) {
      if ( m_run.isEmpty() || run != m_run ) return;
      finish( !error.isEmpty() ? QStringLiteral( "error" ) : aborted ? QStringLiteral( "cancelled" ) : QStringLiteral( "ok" ) );
    } );
}

ConversationMetrics::~ConversationMetrics()
{
  // Closing/destroying an active adapter cannot masquerade as a completed run.
  if ( !m_run.isEmpty() ) finish( QStringLiteral( "incomplete" ) );
}

void ConversationMetrics::begin( const QString &run )
{
  if ( run.isEmpty() ) return;
  if ( !m_run.isEmpty() ) finish( QStringLiteral( "incomplete" ) );
  m_snapshot = {};
  if ( !Trace::enabled() ) return;
  m_run = run;
  m_requests = m_reports = m_invalidReports = 0;
  m_firstDeltaUs = -1;
  m_totals = zeroUsage();
  m_elapsed.start();
}

void ConversationMetrics::request( const QString &run )
{
  if ( m_run.isEmpty() || run != m_run ) return;
  ++m_requests;
}

void ConversationMetrics::usage( const QString &run, const QJsonObject &envelope )
{
  if ( m_run.isEmpty() || run != m_run ) return;
  // The protocol/transport already enforces one usage event per completion.
  // Ignore all other envelope fields, including cumulativeUsage and cost.
  // An unexpected extra event is diagnostic only, never a control failure.
  const QJsonObject delta = envelope.value( QStringLiteral( "usage" ) ).toObject();
  if ( delta.isEmpty() || m_reports >= m_requests ) { ++m_invalidReports; return; }
  QJsonObject totals = m_totals;
  for ( const auto &key : kUsageFields )
  {
    const QJsonValue source = delta.value( key );
    const double value = source.toDouble( -1 );
    const double total = totals.value( key ).toDouble() + value;
    if ( !source.isDouble() || !std::isfinite( value ) || value < 0 || std::floor( value ) != value
         || value > kMaxSafeInteger || total > kMaxSafeInteger )
    { ++m_invalidReports; return; }
    totals[key] = total;
  }
  m_totals = totals;
  ++m_reports;
}

void ConversationMetrics::firstDelta( const QString &run )
{
  if ( m_run.isEmpty() || run != m_run || m_firstDeltaUs >= 0 ) return;
  m_firstDeltaUs = m_elapsed.nsecsElapsed() / 1000;
}

void ConversationMetrics::finish( const QString &status )
{
  if ( m_run.isEmpty() ) return;
  const QString run = m_run;
  m_run.clear(); // Clear before publishing, so a reentrant consumer cannot settle twice.
  const qint64 elapsedUs = m_elapsed.nsecsElapsed() / 1000;
  m_snapshot = {
    { QStringLiteral( "schema" ), QStringLiteral( "sicnu.agent-metrics/1" ) },
    { QStringLiteral( "backend" ), m_backend == Backend::Pi ? QStringLiteral( "pi" ) : QStringLiteral( "native" ) },
    { QStringLiteral( "status" ), status },
    { QStringLiteral( "model_request_attempts" ), static_cast<double>( m_requests ) },
    { QStringLiteral( "usage_reports" ), static_cast<double>( m_reports ) },
    { QStringLiteral( "invalid_usage_reports" ), static_cast<double>( m_invalidReports ) },
    { QStringLiteral( "usage_coverage" ), m_requests == 0 ? QStringLiteral( "no_requests" )
        : m_invalidReports ? QStringLiteral( "invalid" ) : m_reports == 0 ? QStringLiteral( "missing" )
        : m_reports < m_requests ? QStringLiteral( "partial" ) : QStringLiteral( "reported" ) },
    { QStringLiteral( "token_source" ), m_backend == Backend::Pi ? QStringLiteral( "pi_sdk" ) : QStringLiteral( "provider_sse" ) },
    { QStringLiteral( "tokens" ), m_reports ? QJsonValue( m_totals ) : QJsonValue( QJsonValue::Null ) },
    { QStringLiteral( "elapsed_us" ), static_cast<double>( elapsedUs ) },
    { QStringLiteral( "first_model_delta_us" ), m_firstDeltaUs < 0 ? QJsonValue( QJsonValue::Null ) : QJsonValue( static_cast<double>( m_firstDeltaUs ) ) },
    { QStringLiteral( "run_error_count" ), status == QLatin1String( "error" ) ? 1 : 0 },
    { QStringLiteral( "cost_status" ), QStringLiteral( "unpriced" ) },
    { QStringLiteral( "cost" ), QJsonValue( QJsonValue::Null ) }
  };
  TraceEvent event;
  event.run = run.toStdString();
  event.op = "agent.conversation";
  event.event = "agent.run.metrics";
  event.phase = "end";
  event.status = status.toStdString();
  event.durationUs = elapsedUs;
  event.detail = QJsonDocument( m_snapshot ).toJson( QJsonDocument::Compact ).toStdString();
  Trace::publish( event );
}

} // namespace sicnu::agent::runtime
