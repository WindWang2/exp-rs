// test_copilot_history_retention.cpp — issue #1056.
//
// The copilot dock grows its request history by 2-3 entries per tool round.
// trimMessageHistory() caps it without ever handing the LLM an invalid
// conversation: the system prompt stays at index 0, the retained suffix must
// start at a user turn, and an assistant tool_calls entry must always be
// immediately followed by the tool result for the same tool_call_id.
#include <catch2/catch_test_macros.hpp>

#include "agent/copilot_history_retention.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace
{

QJsonObject systemMessage()
{
  QJsonObject message;
  message[QStringLiteral( "role" )] = QStringLiteral( "system" );
  message[QStringLiteral( "content" )] = QStringLiteral( "system prompt" );
  return message;
}

QJsonObject userMessage( const QString &content )
{
  QJsonObject message;
  message[QStringLiteral( "role" )] = QStringLiteral( "user" );
  message[QStringLiteral( "content" )] = content;
  return message;
}

/// Mirrors the assistant tool_calls shape built by the copilot dock.
QJsonObject assistantToolCallMessage( const QString &toolCallId )
{
  QJsonObject function;
  function[QStringLiteral( "name" )] = QStringLiteral( "spatial:raster_inspect" );
  function[QStringLiteral( "arguments" )] = QStringLiteral( "{}" );

  QJsonObject call;
  call[QStringLiteral( "id" )] = toolCallId;
  call[QStringLiteral( "type" )] = QStringLiteral( "function" );
  call[QStringLiteral( "function" )] = function;

  QJsonArray toolCalls;
  toolCalls.append( call );

  QJsonObject message;
  message[QStringLiteral( "role" )] = QStringLiteral( "assistant" );
  message[QStringLiteral( "content" )] = QString();
  message[QStringLiteral( "tool_calls" )] = toolCalls;
  return message;
}

QJsonObject toolResultMessage( const QString &toolCallId )
{
  QJsonObject message;
  message[QStringLiteral( "role" )] = QStringLiteral( "tool" );
  message[QStringLiteral( "tool_call_id" )] = toolCallId;
  message[QStringLiteral( "content" )] = QStringLiteral( "ok" );
  return message;
}

/// The validity property the hard cap must never break: every assistant
/// tool_calls entry is immediately followed by the matching tool result.
void checkToolExchangePairing( const QJsonArray &history )
{
  for ( int i = 0; i < history.size(); ++i )
  {
    const QJsonObject message = history.at( i ).toObject();
    if ( message.value( QStringLiteral( "role" ) ).toString() != QLatin1String( "assistant" ) )
      continue;
    const QJsonArray toolCalls = message.value( QStringLiteral( "tool_calls" ) ).toArray();
    if ( toolCalls.isEmpty() )
      continue;

    REQUIRE( i + 1 < history.size() );
    const QJsonObject next = history.at( i + 1 ).toObject();
    REQUIRE( next.value( QStringLiteral( "role" ) ).toString() == QLatin1String( "tool" ) );
    REQUIRE( next.value( QStringLiteral( "tool_call_id" ) ).toString()
             == toolCalls.at( 0 ).toObject().value( QStringLiteral( "id" ) ).toString() );
  }
}

void appendToolCallTurns( QJsonArray &history, int firstTurn, int lastTurn )
{
  for ( int i = firstTurn; i <= lastTurn; ++i )
  {
    history.append( userMessage( QStringLiteral( "turn %1" ).arg( i ) ) );
    history.append( assistantToolCallMessage( QStringLiteral( "call-%1" ).arg( i ) ) );
    history.append( toolResultMessage( QStringLiteral( "call-%1" ).arg( i ) ) );
  }
}

} // namespace

TEST_CASE( "Copilot retention: history under the cap is untouched", "[agent][copilot][retention][1056]" )
{
  QJsonArray history;
  history.append( systemMessage() );
  appendToolCallTurns( history, 1, 10 );
  REQUIRE( history.size() < sicnu::agent::kMaxHistoryMessages );

  const QJsonArray before = history;
  sicnu::agent::trimMessageHistory( history );

  CHECK( history == before );
}

TEST_CASE( "Copilot retention: over the cap keeps the system prompt and whole turns", "[agent][copilot][retention][1056]" )
{
  QJsonArray history;
  history.append( systemMessage() );
  appendToolCallTurns( history, 1, 40 );
  REQUIRE( history.size() > sicnu::agent::kMaxHistoryMessages );

  sicnu::agent::trimMessageHistory( history );

  // One slot above the cap: the system prompt is always kept on top of the
  // kMaxHistoryMessages most recent entries.
  CHECK( history.size() <= sicnu::agent::kMaxHistoryMessages + 1 );
  REQUIRE( history.size() >= 2 );
  CHECK( history.at( 0 ).toObject().value( QStringLiteral( "role" ) ).toString()
         == QLatin1String( "system" ) );
  CHECK( history.at( 1 ).toObject().value( QStringLiteral( "role" ) ).toString()
         == QLatin1String( "user" ) );
  checkToolExchangePairing( history );
}

TEST_CASE( "Copilot retention: no user boundary at the cap leaves the array unchanged", "[agent][copilot][retention][1056]" )
{
  // [system, user, assistant(tool_calls), tool, ...]: every user entry sits
  // before the cap boundary, so there is no valid restart point.
  QJsonArray history;
  history.append( systemMessage() );
  history.append( userMessage( QStringLiteral( "only early user turn" ) ) );
  for ( int i = 0; i < 60; ++i )
  {
    history.append( assistantToolCallMessage( QStringLiteral( "call-%1" ).arg( i ) ) );
    history.append( toolResultMessage( QStringLiteral( "call-%1" ).arg( i ) ) );
  }
  REQUIRE( history.size() > sicnu::agent::kMaxHistoryMessages );

  const QJsonArray before = history;
  sicnu::agent::trimMessageHistory( history );

  // Validity wins over the cap: no safe boundary means no trimming at all.
  CHECK( history == before );
  CHECK( history.size() > sicnu::agent::kMaxHistoryMessages );
  checkToolExchangePairing( history );
}

TEST_CASE( "Copilot retention: retained suffix starts at the first user boundary at/after the cap", "[agent][copilot][retention][1056]" )
{
  QJsonArray history;
  history.append( systemMessage() );
  history.append( userMessage( QStringLiteral( "early boundary" ) ) );
  for ( int i = 0; i < 90; ++i )
  {
    history.append( assistantToolCallMessage( QStringLiteral( "call-%1" ).arg( i ) ) );
    history.append( toolResultMessage( QStringLiteral( "call-%1" ).arg( i ) ) );
  }
  history.append( userMessage( QStringLiteral( "late boundary" ) ) );
  history.append( assistantToolCallMessage( QStringLiteral( "call-late" ) ) );
  history.append( toolResultMessage( QStringLiteral( "call-late" ) ) );
  REQUIRE( history.size() > sicnu::agent::kMaxHistoryMessages );

  sicnu::agent::trimMessageHistory( history );

  // The early user entry is before the cap boundary and must NOT be chosen;
  // the retained suffix starts exactly at the late boundary.
  QJsonArray expected;
  expected.append( systemMessage() );
  expected.append( userMessage( QStringLiteral( "late boundary" ) ) );
  expected.append( assistantToolCallMessage( QStringLiteral( "call-late" ) ) );
  expected.append( toolResultMessage( QStringLiteral( "call-late" ) ) );
  CHECK( history == expected );
  checkToolExchangePairing( history );
}
