#include <QJsonObject>
#include <catch2/catch_test_macros.hpp>

#include "agent/runtime/conversation_metrics.h"
#include "runtime/observability/trace.h"

#include <QJsonDocument>

#include <memory>

using namespace sicnu::agent::runtime;
namespace trace = sicnu::runtime::observability::trace;

namespace
{
class ObservedRuntime final : public AgentConversationRuntime
{
  public:
    int cancellations = 0;
    int toolCompletions = 0;
    void startSession( const ConversationConfig & ) override { emit ready(); }
    bool submitPrompt( const QString &run, const QString & ) override { emit runStarted( run ); return true; }
    bool completeTool( const QString &, const QString &, const QJsonObject & ) override { ++toolCompletions; return true; }
    void cancelRun() override { ++cancellations; }
    void closeSession() override {}
    bool isReady() const override { return true; }
    void request( const QString &run ) { emit modelRequestStarted( run ); }
    void usage( const QString &run, const QJsonObject &value ) { emit usageUpdated( run, value ); }
    void content( const QString &run, const QString &value ) { emit contentDelta( run, value ); }
    void end( const QString &run, bool aborted = false, const QString &error = {} ) { emit settled( run, aborted, error ); }
};

struct TraceScope
{
  std::shared_ptr<trace::ITraceSink> previous = trace::Trace::sink();
  std::shared_ptr<trace::RingTraceSink> sink = std::make_shared<trace::RingTraceSink>( 32 );
  TraceScope() { trace::Trace::install( sink ); }
  ~TraceScope() { trace::Trace::install( previous ); }
};

QJsonObject usage( int input = 30, int output = 20 )
{
  return { { QStringLiteral( "usage" ), QJsonObject{
    { QStringLiteral( "input" ), input }, { QStringLiteral( "output" ), output },
    { QStringLiteral( "cacheRead" ), 0 }, { QStringLiteral( "cacheWrite" ), 0 },
    { QStringLiteral( "totalTokens" ), input + output } } } };
}
}

TEST_CASE( "Conversation metrics sums delta usage once and never records payloads or SDK pricing",
           "[agent][runtime][metrics]" )
{
  TraceScope scope;
  ObservedRuntime runtime;
  ConversationMetrics metrics( &runtime, ConversationMetrics::Backend::Pi );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "run-1" ), QStringLiteral( "private-prompt" ) ) );
  runtime.request( QStringLiteral( "run-1" ) );
  runtime.content( QStringLiteral( "run-1" ), QStringLiteral( "private-model-content" ) );
  auto delta = usage();
  auto fields = delta[QStringLiteral( "usage" )].toObject();
  fields[QStringLiteral( "cost" )] = QJsonObject{ { QStringLiteral( "total" ), 0 } };
  fields[QStringLiteral( "prompt" )] = QStringLiteral( "fixture-secret" );
  delta[QStringLiteral( "usage" )] = fields;
  delta[QStringLiteral( "cumulativeUsage" )] = usage( 9000, 9000 )[QStringLiteral( "usage" )];
  runtime.usage( QStringLiteral( "run-1" ), delta );
  runtime.request( QStringLiteral( "run-1" ) ); // Result-only compaction has no content/card.
  runtime.usage( QStringLiteral( "run-1" ), usage( 3, 2 ) );
  runtime.end( QStringLiteral( "run-1" ) );
  runtime.end( QStringLiteral( "run-1" ) );
  const auto record = metrics.snapshot();
  CHECK( record[QStringLiteral( "model_request_attempts" )].toInt() == 2 );
  CHECK( record[QStringLiteral( "usage_reports" )].toInt() == 2 );
  CHECK( record[QStringLiteral( "usage_coverage" )].toString() == QStringLiteral( "reported" ) );
  CHECK( record[QStringLiteral( "tokens" )].toObject()[QStringLiteral( "totalTokens" )].toInt() == 55 );
  CHECK( record[QStringLiteral( "cost_status" )].toString() == QStringLiteral( "unpriced" ) );
  CHECK( record[QStringLiteral( "cost" )].isNull() );
  CHECK( record[QStringLiteral( "elapsed_us" )].toDouble() >= 0 );
  CHECK( record[QStringLiteral( "first_model_delta_us" )].toDouble( -1 ) >= 0 );
  REQUIRE( scope.sink->size() == 1 );
  const auto event = scope.sink->snapshot().front();
  CHECK( event.event == "agent.run.metrics" );
  CHECK( event.run == "run-1" );
  const QByteArray encoded = QByteArray::fromStdString( trace::encodeNdjson( event ) );
  CHECK_FALSE( encoded.contains( "private-prompt" ) );
  CHECK_FALSE( encoded.contains( "private-model-content" ) );
  CHECK_FALSE( encoded.contains( "fixture-secret" ) );
  CHECK_FALSE( encoded.contains( "cumulativeUsage" ) );
  CHECK( runtime.cancellations == 0 );
  CHECK( runtime.toolCompletions == 0 );
}

TEST_CASE( "Conversation metrics preserves missing and partial usage and counts failed runs separately",
           "[agent][runtime][metrics]" )
{
  TraceScope scope;
  ObservedRuntime runtime;
  ConversationMetrics metrics( &runtime, ConversationMetrics::Backend::Native );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "failed" ), {} ) );
  runtime.request( QStringLiteral( "failed" ) );
  runtime.content( QStringLiteral( "failed" ), {} );
  runtime.end( QStringLiteral( "failed" ), false, QStringLiteral( "raw-provider-secret" ) );
  CHECK( metrics.snapshot()[QStringLiteral( "tokens" )].isNull() );
  CHECK( metrics.snapshot()[QStringLiteral( "usage_coverage" )].toString() == QStringLiteral( "missing" ) );
  CHECK( metrics.snapshot()[QStringLiteral( "status" )].toString() == QStringLiteral( "error" ) );
  CHECK( metrics.snapshot()[QStringLiteral( "run_error_count" )].toInt() == 1 );
  CHECK( metrics.snapshot()[QStringLiteral( "first_model_delta_us" )].isNull() );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "cancelled" ), {} ) );
  runtime.request( QStringLiteral( "cancelled" ) );
  runtime.usage( QStringLiteral( "cancelled" ), usage() );
  runtime.request( QStringLiteral( "cancelled" ) );
  runtime.end( QStringLiteral( "cancelled" ), true );
  CHECK( metrics.snapshot()[QStringLiteral( "usage_coverage" )].toString() == QStringLiteral( "partial" ) );
  CHECK( metrics.snapshot()[QStringLiteral( "status" )].toString() == QStringLiteral( "cancelled" ) );
  CHECK( metrics.snapshot()[QStringLiteral( "run_error_count" )].toInt() == 0 );
  CHECK( scope.sink->size() == 2 );
  for ( const auto &event : scope.sink->snapshot() ) CHECK_FALSE( event.detail.find( "raw-provider-secret" ) != std::string::npos );
}

TEST_CASE( "Conversation metrics rejects malformed statistics without changing runtime authority",
           "[agent][runtime][metrics]" )
{
  TraceScope scope;
  ObservedRuntime runtime;
  ConversationMetrics metrics( &runtime, ConversationMetrics::Backend::Native );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "run" ), {} ) );
  runtime.request( QStringLiteral( "run" ) );
  auto invalid = usage();
  auto fields = invalid[QStringLiteral( "usage" )].toObject();
  fields[QStringLiteral( "input" )] = -1;
  invalid[QStringLiteral( "usage" )] = fields;
  runtime.usage( QStringLiteral( "run" ), invalid );
  runtime.usage( QStringLiteral( "stale" ), usage() );
  runtime.request( QStringLiteral( "stale" ) );
  runtime.usage( QStringLiteral( "run" ), usage() );
  runtime.usage( QStringLiteral( "run" ), usage() ); // Duplicate report cannot double count.
  runtime.end( QStringLiteral( "stale" ) );
  CHECK( scope.sink->size() == 0 );
  runtime.end( QStringLiteral( "run" ) );
  CHECK( metrics.snapshot()[QStringLiteral( "status" )].toString() == QStringLiteral( "ok" ) );
  CHECK( metrics.snapshot()[QStringLiteral( "usage_coverage" )].toString() == QStringLiteral( "invalid" ) );
  CHECK( metrics.snapshot()[QStringLiteral( "invalid_usage_reports" )].toInt() == 2 );
  CHECK( metrics.snapshot()[QStringLiteral( "tokens" )].toObject()[QStringLiteral( "totalTokens" )].toInt() == 50 );
  CHECK( runtime.cancellations == 0 );
  CHECK( runtime.toolCompletions == 0 );
}

TEST_CASE( "Conversation metrics is opt in and exposes discarded active runs as incomplete",
           "[agent][runtime][metrics]" )
{
  TraceScope scope;
  ObservedRuntime runtime;
  {
    ConversationMetrics metrics( &runtime, ConversationMetrics::Backend::Native );
    trace::Trace::install( nullptr );
    runtime.submitPrompt( QStringLiteral( "trace-off" ), {} );
    runtime.request( QStringLiteral( "trace-off" ) );
    runtime.usage( QStringLiteral( "trace-off" ), usage() );
    runtime.end( QStringLiteral( "trace-off" ) );
    CHECK( metrics.snapshot().isEmpty() );
    trace::Trace::install( scope.sink );
    runtime.submitPrompt( QStringLiteral( "discarded" ), {} );
    runtime.request( QStringLiteral( "discarded" ) );
  }
  REQUIRE( scope.sink->size() == 1 );
  CHECK( scope.sink->snapshot().front().status == "incomplete" );
  CHECK( runtime.cancellations == 0 );
}
