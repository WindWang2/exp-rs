#include <catch2/catch_test_macros.hpp>

#include "agent/runtime/conversation_checkpoint_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>

using sicnu::agent::runtime::ConversationCheckpointBinding;
using sicnu::agent::runtime::ConversationCheckpointStore;
using sicnu::agent::runtime::ConversationCheckpointWriteResult;
using sicnu::agent::runtime::ConversationExecutionReference;

namespace
{
void application()
{
  if ( QCoreApplication::instance() ) return;
  static int argc = 1;
  static char name[] = "test_conversation_checkpoint_store";
  static char *argv[] = { name, nullptr };
  new QCoreApplication( argc, argv );
}

ConversationCheckpointBinding binding()
{
  return { QStringLiteral( "saved-project-id" ), QStringLiteral( "conversation-id" ), QString( 64, QLatin1Char( 'a' ) ),
    QStringLiteral( "catalog-revision" ), QString( 64, QLatin1Char( 'b' ) ) };
}

QByteArray checkpoint( const ConversationCheckpointBinding &ids, const QString &checkpointId = QStringLiteral( "checkpoint-1" ),
                       const QString &runId = QStringLiteral( "run-1" ), const QString &answer = QStringLiteral( "Earlier approved work completed." ) )
{
  const QString timestamp = QStringLiteral( "2026-11-15T00:00:00.000Z" );
  const QJsonObject cost{ { QStringLiteral( "input" ), 0 }, { QStringLiteral( "output" ), 0 }, { QStringLiteral( "cacheRead" ), 0 },
    { QStringLiteral( "cacheWrite" ), 0 }, { QStringLiteral( "total" ), 0 } };
  const QJsonObject usage{ { QStringLiteral( "input" ), 30 }, { QStringLiteral( "output" ), 20 }, { QStringLiteral( "cacheRead" ), 0 },
    { QStringLiteral( "cacheWrite" ), 0 }, { QStringLiteral( "totalTokens" ), 50 }, { QStringLiteral( "cost" ), cost } };
  const QJsonObject user{ { QStringLiteral( "type" ), QStringLiteral( "message" ) }, { QStringLiteral( "id" ), QStringLiteral( "user-entry" ) },
    { QStringLiteral( "parentId" ), QJsonValue::Null }, { QStringLiteral( "timestamp" ), timestamp }, { QStringLiteral( "message" ), QJsonObject{
      { QStringLiteral( "role" ), QStringLiteral( "user" ) }, { QStringLiteral( "content" ), QStringLiteral( "Fixture request" ) }, { QStringLiteral( "timestamp" ), 1 } } } };
  const QJsonObject assistant{ { QStringLiteral( "type" ), QStringLiteral( "message" ) }, { QStringLiteral( "id" ), QStringLiteral( "assistant-entry" ) },
    { QStringLiteral( "parentId" ), QStringLiteral( "user-entry" ) }, { QStringLiteral( "timestamp" ), timestamp }, { QStringLiteral( "message" ), QJsonObject{
      { QStringLiteral( "role" ), QStringLiteral( "assistant" ) }, { QStringLiteral( "content" ), QJsonArray{ QJsonObject{
        { QStringLiteral( "type" ), QStringLiteral( "text" ) }, { QStringLiteral( "text" ), answer } } } },
      { QStringLiteral( "api" ), QStringLiteral( "openai-completions" ) }, { QStringLiteral( "provider" ), QStringLiteral( "exp-rs-product" ) },
      { QStringLiteral( "model" ), QStringLiteral( "fixture-model" ) }, { QStringLiteral( "usage" ), usage },
      { QStringLiteral( "stopReason" ), QStringLiteral( "stop" ) }, { QStringLiteral( "timestamp" ), 1 } } } };
  const QJsonObject document{
    { QStringLiteral( "schema" ), QStringLiteral( "sicnu.pi-checkpoint/1" ) }, { QStringLiteral( "piVersion" ), QStringLiteral( "1.5.2" ) },
    { QStringLiteral( "checkpointId" ), checkpointId }, { QStringLiteral( "completedRunId" ), runId },
    { QStringLiteral( "projectId" ), ids.projectId }, { QStringLiteral( "conversationId" ), ids.conversationId },
    { QStringLiteral( "sdkReceiptSha256" ), ids.sdkReceiptSha256 }, { QStringLiteral( "toolRevision" ), ids.toolRevision },
    { QStringLiteral( "systemPromptSha256" ), ids.systemPromptSha256 }, { QStringLiteral( "status" ), QStringLiteral( "completed" ) },
    { QStringLiteral( "header" ), QJsonObject{ { QStringLiteral( "type" ), QStringLiteral( "session" ) }, { QStringLiteral( "version" ), 3 },
      { QStringLiteral( "id" ), QStringLiteral( "sdk-session" ) }, { QStringLiteral( "timestamp" ), timestamp }, { QStringLiteral( "cwd" ), QStringLiteral( "C:/owned-fixture" ) } } },
    { QStringLiteral( "entries" ), QJsonArray{ user, assistant } }, { QStringLiteral( "leafId" ), QStringLiteral( "assistant-entry" ) }
  };
  return QJsonDocument( document ).toJson( QJsonDocument::Compact );
}

void succeeded( const ConversationCheckpointWriteResult &result )
{
  CAPTURE( result.error.toStdString() );
  REQUIRE( result.error.isEmpty() );
  REQUIRE( result.committed );
}

QString statePath( const QString &root )
{
  const auto files = QDir( root ).entryList( { QStringLiteral( "*.conversation.json" ) }, QDir::Files );
  REQUIRE( files.size() == 1 );
  return QDir( root ).filePath( files.first() );
}

QByteArray readBytes( const QString &path )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  return file.readAll();
}

void writeBytes( const QString &path, const QByteArray &bytes )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  REQUIRE( file.write( bytes ) == bytes.size() );
}
} // namespace

TEST_CASE( "Conversation checkpoint load is read only and restart retains only data", "[agent][checkpoint_store]" )
{
  application();
  QTemporaryDir directory;
  REQUIRE( directory.isValid() );
  const QString root = QDir( directory.path() ).filePath( QStringLiteral( "private/conversations" ) );
  const auto ids = binding();
  ConversationCheckpointStore store( root );
  const auto fresh = store.load( ids );
  REQUIRE( fresh.error.isEmpty() );
  REQUIRE_FALSE( fresh.found );
  REQUIRE_FALSE( QDir( root ).exists() );
  succeeded( store.markRunStarted( ids, QStringLiteral( "run-1" ) ) );
  const auto markerOnly = ConversationCheckpointStore( root ).load( ids );
  REQUIRE( markerOnly.error.isEmpty() );
  REQUIRE( markerOnly.found );
  REQUIRE( markerOnly.checkpointJson.isEmpty() );
  REQUIRE( markerOnly.interruptedRunId == QStringLiteral( "run-1" ) );
  const QByteArray payload = checkpoint( ids );
  succeeded( store.saveCompleted( ids, payload ) );
  const auto restored = ConversationCheckpointStore( root ).load( ids );
  REQUIRE( restored.error.isEmpty() );
  REQUIRE( restored.checkpointJson == payload );
  REQUIRE( restored.interruptedRunId.isEmpty() );
  REQUIRE( restored.checkpointSha256 == QString::fromLatin1( QCryptographicHash::hash( payload, QCryptographicHash::Sha256 ).toHex() ) );
  const auto disk = QJsonDocument::fromJson( readBytes( statePath( root ) ) ).object();
  REQUIRE( disk.size() == 7 );
  REQUIRE_FALSE( disk.contains( QStringLiteral( "profile" ) ) );
  REQUIRE_FALSE( disk.contains( QStringLiteral( "apiKey" ) ) );
}

TEST_CASE( "Interrupted conversation preserves the stable checkpoint and requires an exact acknowledgement", "[agent][checkpoint_store]" )
{
  application();
  QTemporaryDir directory;
  REQUIRE( directory.isValid() );
  const auto ids = binding();
  ConversationCheckpointStore store( directory.path() );
  const QByteArray first = checkpoint( ids );
  succeeded( store.saveCompleted( ids, first ) );
  succeeded( store.markRunStarted( ids, QStringLiteral( "run-2" ) ) );
  const QString path = statePath( directory.path() );
  const QByteArray before = readBytes( path );
  const auto interrupted = ConversationCheckpointStore( directory.path() ).load( ids );
  REQUIRE( interrupted.error.isEmpty() );
  REQUIRE( interrupted.checkpointJson == first );
  REQUIRE( interrupted.interruptedRunId == QStringLiteral( "run-2" ) );
  REQUIRE( readBytes( path ) == before ); // Loading never consumes/replays the interruption.
  REQUIRE( store.saveCompleted( ids, first ).error == QStringLiteral( "checkpoint_stale_run" ) );
  REQUIRE( store.markRunStarted( ids, QStringLiteral( "run-3" ) ).error == QStringLiteral( "checkpoint_interruption_pending" ) );
  REQUIRE( store.acknowledgeInterruption( ids, QStringLiteral( "run-old" ) ).error == QStringLiteral( "checkpoint_interruption_mismatch" ) );
  REQUIRE( readBytes( path ) == before );
  succeeded( store.acknowledgeInterruption( ids, QStringLiteral( "run-2" ) ) );
  REQUIRE( store.load( ids ).checkpointJson == first );
  REQUIRE( store.load( ids ).interruptedRunId.isEmpty() );
  REQUIRE( store.markRunStarted( ids, QStringLiteral( "run-1" ) ).error == QStringLiteral( "checkpoint_run_reused" ) );
  succeeded( store.markRunStarted( ids, QStringLiteral( "run-3" ) ) );
  succeeded( store.saveCompleted( ids, checkpoint( ids, QStringLiteral( "checkpoint-3" ), QStringLiteral( "run-3" ) ) ) );
  const auto completed = store.load( ids );
  REQUIRE( completed.error.isEmpty() );
  REQUIRE( completed.interruptedRunId.isEmpty() );
  REQUIRE( QJsonDocument::fromJson( completed.checkpointJson ).object().value( QStringLiteral( "completedRunId" ) ).toString() == QStringLiteral( "run-3" ) );
}

TEST_CASE( "Invalid secret-bearing envelopes and oversized checkpoints preserve prior bytes and marker", "[agent][checkpoint_store]" )
{
  application();
  QTemporaryDir directory;
  REQUIRE( directory.isValid() );
  const auto ids = binding();
  ConversationCheckpointStore store( directory.path() );
  succeeded( store.saveCompleted( ids, checkpoint( ids ) ) );
  succeeded( store.markRunStarted( ids, QStringLiteral( "run-2" ) ) );
  succeeded( store.recordOwnedExecutions( ids, QStringLiteral( "run-2" ), {
    { ConversationExecutionReference::Kind::Task, 12, QStringLiteral( "job-12" ), QStringLiteral( "11111111-1111-4111-8111-111111111111" ) }
  } ) );
  const QString path = statePath( directory.path() );
  const QByteArray before = readBytes( path );
  for ( const QString &key : { QStringLiteral( "credentials" ), QStringLiteral( "profile" ), QStringLiteral( "baseUrl" ), QStringLiteral( "apiKey" ) } )
  {
    auto value = QJsonDocument::fromJson( checkpoint( ids, QStringLiteral( "checkpoint-2" ), QStringLiteral( "run-2" ) ) ).object();
    value.insert( key, QStringLiteral( "fixture-secret-must-not-land" ) );
    REQUIRE( store.saveCompleted( ids, QJsonDocument( value ).toJson( QJsonDocument::Compact ) ).error == QStringLiteral( "checkpoint_invalid" ) );
    REQUIRE( readBytes( path ) == before );
  }
  REQUIRE( store.saveCompleted( ids, QByteArray( "{bad" ) ).error == QStringLiteral( "checkpoint_invalid_json" ) );
  REQUIRE( store.saveCompleted( ids, QByteArray( ConversationCheckpointStore::kMaxCheckpointBytes + 1, 'x' ) ).error == QStringLiteral( "checkpoint_too_large" ) );
  REQUIRE( readBytes( path ) == before );
  REQUIRE( store.load( ids ).interruptedRunId == QStringLiteral( "run-2" ) );
  REQUIRE( store.load( ids ).executionRefs.size() == 1 );
}

TEST_CASE( "Checkpoint cap includes exact UTF8 JSON bytes and accepts one MiB", "[agent][checkpoint_store]" )
{
  application();
  QTemporaryDir directory;
  REQUIRE( directory.isValid() );
  const auto ids = binding();
  ConversationCheckpointStore store( directory.path() );
  const qsizetype overhead = checkpoint( ids, QStringLiteral( "checkpoint-1" ), QStringLiteral( "run-1" ), {} ).size();
  const QByteArray exact = checkpoint( ids, QStringLiteral( "checkpoint-1" ), QStringLiteral( "run-1" ),
    QString( ConversationCheckpointStore::kMaxCheckpointBytes - overhead, QLatin1Char( 'x' ) ) );
  REQUIRE( exact.size() == ConversationCheckpointStore::kMaxCheckpointBytes );
  succeeded( store.saveCompleted( ids, exact ) );
  REQUIRE( store.load( ids ).checkpointJson == exact );
  const QByteArray before = readBytes( statePath( directory.path() ) );
  REQUIRE( store.saveCompleted( ids, exact + ' ' ).error == QStringLiteral( "checkpoint_too_large" ) );
  REQUIRE( readBytes( statePath( directory.path() ) ) == before );
  const QByteArray multibyte = checkpoint( ids, QStringLiteral( "checkpoint-1" ), QStringLiteral( "run-1" ),
    QString( ConversationCheckpointStore::kMaxCheckpointBytes / 2, QChar( 0x4e2d ) ) );
  REQUIRE( store.saveCompleted( ids, multibyte ).error == QStringLiteral( "checkpoint_too_large" ) );
}

TEST_CASE( "Checkpoint binding drift and future state remain refused without replacement", "[agent][checkpoint_store]" )
{
  application();
  QTemporaryDir directory;
  REQUIRE( directory.isValid() );
  const auto ids = binding();
  ConversationCheckpointStore store( directory.path() );
  succeeded( store.saveCompleted( ids, checkpoint( ids ) ) );
  const QString path = statePath( directory.path() );
  const QByteArray before = readBytes( path );
  auto changed = ids;
  changed.toolRevision = QStringLiteral( "changed-catalog" );
  REQUIRE( store.load( changed ).error == QStringLiteral( "checkpoint_binding_mismatch" ) );
  REQUIRE( store.saveCompleted( changed, checkpoint( changed ) ).error == QStringLiteral( "checkpoint_binding_mismatch" ) );
  REQUIRE( readBytes( path ) == before );
  auto future = QJsonDocument::fromJson( before ).object();
  future.insert( QStringLiteral( "schema" ), QStringLiteral( "sicnu.pi-conversation-store/2" ) );
  const QByteArray futureBytes = QJsonDocument( future ).toJson( QJsonDocument::Compact );
  writeBytes( path, futureBytes );
  REQUIRE( store.load( ids ).error == QStringLiteral( "checkpoint_store_version_unsupported" ) );
  REQUIRE( store.markRunStarted( ids, QStringLiteral( "run-2" ) ).error == QStringLiteral( "checkpoint_store_version_unsupported" ) );
  REQUIRE( readBytes( path ) == futureBytes );
}

TEST_CASE( "Corrupt checksums and oversized disk state are never treated as a fresh conversation", "[agent][checkpoint_store]" )
{
  application();
  QTemporaryDir directory;
  REQUIRE( directory.isValid() );
  const auto ids = binding();
  ConversationCheckpointStore store( directory.path() );
  succeeded( store.saveCompleted( ids, checkpoint( ids ) ) );
  const QString path = statePath( directory.path() );
  auto corrupt = QJsonDocument::fromJson( readBytes( path ) ).object();
  corrupt.insert( QStringLiteral( "checkpointSha256" ), QString( 64, QLatin1Char( 'f' ) ) );
  const QByteArray corruptBytes = QJsonDocument( corrupt ).toJson( QJsonDocument::Compact );
  writeBytes( path, corruptBytes );
  REQUIRE( store.load( ids ).error == QStringLiteral( "checkpoint_store_corrupt" ) );
  REQUIRE( store.saveCompleted( ids, checkpoint( ids ) ).error == QStringLiteral( "checkpoint_store_corrupt" ) );
  REQUIRE( readBytes( path ) == corruptBytes );
  const QByteArray oversized( ConversationCheckpointStore::kMaxStoreBytes + 1, 'x' );
  writeBytes( path, oversized );
  REQUIRE( store.load( ids ).error == QStringLiteral( "checkpoint_store_corrupt" ) );
  REQUIRE( store.markRunStarted( ids, QStringLiteral( "run-2" ) ).error == QStringLiteral( "checkpoint_store_corrupt" ) );
  REQUIRE( readBytes( path ) == oversized );
}

TEST_CASE( "Stable IDs select isolated hashed paths and a process lock preserves state", "[agent][checkpoint_store]" )
{
  application();
  QTemporaryDir directory;
  REQUIRE( directory.isValid() );
  auto ids = binding();
  ids.projectId = QStringLiteral( "../project/changes.tif" );
  ids.conversationId = QStringLiteral( "../conversation" );
  ConversationCheckpointStore store( directory.path() );
  succeeded( store.saveCompleted( ids, checkpoint( ids ) ) );
  const QString path = statePath( directory.path() );
  REQUIRE( QFileInfo( path ).absolutePath() == directory.path() );
  REQUIRE( QFileInfo( path ).fileName().size() == 64 + QStringLiteral( ".conversation.json" ).size() );
  auto other = ids;
  other.conversationId = QStringLiteral( "another-conversation" );
  REQUIRE_FALSE( store.load( other ).found );
  const QByteArray before = readBytes( path );
  QLockFile competingWriter( path + QStringLiteral( ".lock" ) );
  REQUIRE( competingWriter.tryLock( 0 ) );
  REQUIRE( store.markRunStarted( ids, QStringLiteral( "run-2" ) ).error == QStringLiteral( "checkpoint_store_busy" ) );
  REQUIRE( readBytes( path ) == before );
  competingWriter.unlock();
  succeeded( store.saveCompleted( other, checkpoint( other ) ) );
  REQUIRE( QDir( directory.path() ).entryList( { QStringLiteral( "*.conversation.json" ) }, QDir::Files ).size() == 2 );
  REQUIRE( store.load( ids ).checkpointJson == checkpoint( ids ) );
}

TEST_CASE( "Execution references merge only into their exact incomplete run and remain bounded identities", "[agent][checkpoint_store]" )
{
  application();
  QTemporaryDir directory;
  REQUIRE( directory.isValid() );
  const auto ids = binding();
  ConversationCheckpointStore store( directory.path() );
  const QByteArray first = checkpoint( ids );
  succeeded( store.saveCompleted( ids, first ) );
  succeeded( store.markRunStarted( ids, QStringLiteral( "run-2" ) ) );
  const QString boot = QStringLiteral( "11111111-1111-4111-8111-111111111111" );
  const ConversationExecutionReference task{ ConversationExecutionReference::Kind::Task, 7, QStringLiteral( "job-7" ), boot };
  const ConversationExecutionReference pipeline{ ConversationExecutionReference::Kind::Pipeline, 9, {}, boot };
  succeeded( store.recordOwnedExecutions( ids, QStringLiteral( "run-2" ), { task, pipeline } ) );
  const auto recovered = ConversationCheckpointStore( directory.path() ).load( ids );
  REQUIRE( recovered.error.isEmpty() );
  REQUIRE( recovered.checkpointJson == first );
  REQUIRE( recovered.interruptedRunId == QStringLiteral( "run-2" ) );
  REQUIRE( recovered.executionRefs.size() == 2 );
  REQUIRE( recovered.executionRefs[0].nativeId == 7 );
  REQUIRE( recovered.executionRefs[0].jobId == QStringLiteral( "job-7" ) );
  REQUIRE( recovered.executionRefs[0].processBootId == boot );
  REQUIRE( recovered.executionRefs[1].kind == ConversationExecutionReference::Kind::Pipeline );
  auto unknownTask = task;
  unknownTask.jobId.clear();
  succeeded( store.recordOwnedExecutions( ids, QStringLiteral( "run-2" ), { unknownTask } ) );
  REQUIRE( store.load( ids ).executionRefs[0].jobId == QStringLiteral( "job-7" ) );
  const QString path = statePath( directory.path() );
  const QByteArray before = readBytes( path );
  REQUIRE( store.recordOwnedExecutions( ids, QStringLiteral( "wrong-run" ), { task } ).error == QStringLiteral( "checkpoint_stale_run" ) );
  auto conflict = task;
  conflict.jobId = QStringLiteral( "another-job" );
  REQUIRE( store.recordOwnedExecutions( ids, QStringLiteral( "run-2" ), { conflict } ).error == QStringLiteral( "checkpoint_execution_identity_conflict" ) );
  QList<ConversationExecutionReference> excessive;
  for ( qsizetype index = 0; index <= ConversationCheckpointStore::kMaxExecutionRefs; ++index )
    excessive.append( { ConversationExecutionReference::Kind::Task, index + 100, QStringLiteral( "job-%1" ).arg( index ), boot } );
  REQUIRE( store.recordOwnedExecutions( ids, QStringLiteral( "run-2" ), excessive ).error == QStringLiteral( "checkpoint_execution_refs_too_large" ) );
  auto unsafe = task;
  unsafe.jobId = QStringLiteral( "https://profile.invalid/key" );
  REQUIRE( store.recordOwnedExecutions( ids, QStringLiteral( "run-2" ), { unsafe } ).error == QStringLiteral( "checkpoint_execution_ref_invalid" ) );
  REQUIRE( readBytes( path ) == before );
  succeeded( store.acknowledgeInterruption( ids, QStringLiteral( "run-2" ) ) );
  REQUIRE( store.load( ids ).executionRefs.isEmpty() );
  REQUIRE( store.load( ids ).checkpointJson == first );
}
