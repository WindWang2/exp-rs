#include "conversation_checkpoint_store.h"

#include "platform/durable_sidecar.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSet>
#include <QStringDecoder>
#include <QUuid>

#include <algorithm>
#include <cmath>

#include <utility>

namespace sicnu::agent::runtime
{
namespace
{
using Store = ConversationCheckpointStore;
using Binding = ConversationCheckpointBinding;
using WriteResult = ConversationCheckpointWriteResult;
using ExecutionRef = ConversationExecutionReference;

struct State
{
  bool found = false;
  QJsonObject checkpoint;
  QByteArray checkpointJson;
  QString checkpointSha256;
  QString interruptedRunId;
  QList<ExecutionRef> executionRefs;
  QString error;
};

QString sha256( const QByteArray &bytes )
{
  return QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

bool identity( const QString &value )
{
  if ( value.isEmpty() || value.toUtf8().size() > 256 ) return false;
  for ( const QChar character : value )
    if ( character.unicode() < 0x20 || character.unicode() == 0x7f ) return false;
  return true;
}

bool digest( const QString &value )
{
  if ( value.size() != 64 ) return false;
  for ( const QChar character : value )
    if ( !( character >= QLatin1Char( '0' ) && character <= QLatin1Char( '9' ) )
         && !( character >= QLatin1Char( 'a' ) && character <= QLatin1Char( 'f' ) ) ) return false;
  return true;
}

bool validBinding( const Binding &binding )
{
  return identity( binding.projectId ) && identity( binding.conversationId ) && identity( binding.toolRevision )
    && digest( binding.sdkReceiptSha256 ) && digest( binding.systemPromptSha256 );
}

bool safeJobId( const QString &value )
{
  if ( value.isEmpty() ) return true;
  if ( !identity( value ) ) return false;
  for ( const QChar character : value )
    if ( !character.isLetterOrNumber() && character != QLatin1Char( '-' ) && character != QLatin1Char( '_' )
         && character != QLatin1Char( '.' ) && character != QLatin1Char( ':' ) ) return false;
  return true;
}

bool validRef( const ExecutionRef &ref )
{
  const QUuid boot( ref.processBootId );
  return ( ref.kind == ExecutionRef::Kind::Task || ref.kind == ExecutionRef::Kind::Pipeline )
    && ref.nativeId > 0 && ref.nativeId <= 9007199254740991LL && safeJobId( ref.jobId )
    && !boot.isNull() && boot.toString( QUuid::WithoutBraces ) == ref.processBootId
    && ( ref.kind == ExecutionRef::Kind::Task || ref.jobId.isEmpty() );
}

QJsonArray refsToJson( const QList<ExecutionRef> &refs )
{
  QJsonArray array;
  for ( const auto &ref : refs )
    array.append( QJsonObject{ { QStringLiteral( "kind" ), ref.kind == ExecutionRef::Kind::Task ? QStringLiteral( "task" ) : QStringLiteral( "pipeline" ) },
      { QStringLiteral( "nativeId" ), ref.nativeId }, { QStringLiteral( "jobId" ), ref.jobId }, { QStringLiteral( "processBootId" ), ref.processBootId } } );
  return array;
}

bool exactKeys( const QJsonObject &object, const QSet<QString> &keys )
{
  if ( object.size() != keys.size() ) return false;
  for ( auto it = object.begin(); it != object.end(); ++it ) if ( !keys.contains( it.key() ) ) return false;
  return true;
}

QString parseObject( const QByteArray &bytes, QJsonObject &object )
{
  QStringDecoder decoder( QStringDecoder::Utf8, QStringDecoder::Flag::Stateless );
  const QString decoded = decoder.decode( bytes );
  if ( decoder.hasError() || decoded.toUtf8() != bytes ) return QStringLiteral( "checkpoint_invalid_json" );
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson( bytes, &error );
  if ( error.error != QJsonParseError::NoError || !document.isObject() ) return QStringLiteral( "checkpoint_invalid_json" );
  object = document.object();
  return {};
}

QString validateCheckpoint( const Binding &binding, const QByteArray &bytes,
                            QJsonObject &checkpoint, QByteArray &compact )
{
  if ( bytes.size() > Store::kMaxCheckpointBytes ) return QStringLiteral( "checkpoint_too_large" );
  if ( const QString error = parseObject( bytes, checkpoint ); !error.isEmpty() ) return error;
  const QSet<QString> keys{ QStringLiteral( "schema" ), QStringLiteral( "piVersion" ), QStringLiteral( "checkpointId" ),
    QStringLiteral( "conversationId" ), QStringLiteral( "projectId" ), QStringLiteral( "completedRunId" ),
    QStringLiteral( "toolRevision" ), QStringLiteral( "sdkReceiptSha256" ), QStringLiteral( "systemPromptSha256" ),
    QStringLiteral( "status" ), QStringLiteral( "header" ), QStringLiteral( "entries" ), QStringLiteral( "leafId" ) };
  if ( !exactKeys( checkpoint, keys ) ) return QStringLiteral( "checkpoint_invalid" );
  if ( checkpoint.value( QStringLiteral( "schema" ) ).toString() != QLatin1String( "sicnu.pi-checkpoint/1" )
       || checkpoint.value( QStringLiteral( "piVersion" ) ).toString() != QLatin1String( "1.5.2" ) )
    return QStringLiteral( "checkpoint_version_unsupported" );
  if ( checkpoint.value( QStringLiteral( "status" ) ).toString() != QLatin1String( "completed" ) )
    return QStringLiteral( "checkpoint_not_completed" );
  for ( const QString &key : { QStringLiteral( "checkpointId" ), QStringLiteral( "conversationId" ), QStringLiteral( "projectId" ),
                              QStringLiteral( "completedRunId" ), QStringLiteral( "toolRevision" ) } )
    if ( !checkpoint.value( key ).isString() || !identity( checkpoint.value( key ).toString() ) )
      return QStringLiteral( "checkpoint_invalid" );
  if ( checkpoint.value( QStringLiteral( "projectId" ) ).toString() != binding.projectId
       || checkpoint.value( QStringLiteral( "conversationId" ) ).toString() != binding.conversationId
       || checkpoint.value( QStringLiteral( "sdkReceiptSha256" ) ).toString() != binding.sdkReceiptSha256
       || checkpoint.value( QStringLiteral( "toolRevision" ) ).toString() != binding.toolRevision
       || checkpoint.value( QStringLiteral( "systemPromptSha256" ) ).toString() != binding.systemPromptSha256 )
    return QStringLiteral( "checkpoint_binding_mismatch" );
  // SDK entry/tree/tool-pair validation remains in the Node module. Do not
  // create a second implementation of that versioned format in this store.
  const auto header = checkpoint.value( QStringLiteral( "header" ) ).toObject();
  if ( !checkpoint.value( QStringLiteral( "header" ) ).isObject()
       || header.value( QStringLiteral( "type" ) ).toString() != QLatin1String( "session" )
       || header.value( QStringLiteral( "version" ) ).toDouble( -1 ) != 3
       || !header.value( QStringLiteral( "id" ) ).isString() || !identity( header.value( QStringLiteral( "id" ) ).toString() )
       || !checkpoint.value( QStringLiteral( "entries" ) ).isArray() || checkpoint.value( QStringLiteral( "entries" ) ).toArray().isEmpty()
       || !checkpoint.value( QStringLiteral( "leafId" ) ).isString() || !identity( checkpoint.value( QStringLiteral( "leafId" ) ).toString() ) )
    return QStringLiteral( "checkpoint_invalid" );
  compact = QJsonDocument( checkpoint ).toJson( QJsonDocument::Compact );
  if ( compact.size() > Store::kMaxCheckpointBytes ) return QStringLiteral( "checkpoint_too_large" );
  return {};
}

QString storagePath( const QString &directory, const Binding &binding )
{
  const QJsonArray ids{ binding.projectId, binding.conversationId };
  return QDir( directory ).filePath( sha256( QJsonDocument( ids ).toJson( QJsonDocument::Compact ) ) + QStringLiteral( ".conversation.json" ) );
}

QString checkDirectory( const QString &directory, bool create )
{
  if ( !QDir::isAbsolutePath( directory ) || directory.contains( QChar::Null ) || directory.toUtf8().size() > 32768 )
    return QStringLiteral( "checkpoint_store_directory_invalid" );
  QFileInfo info( directory );
  if ( info.isSymLink() ) return QStringLiteral( "checkpoint_store_path_refused" );
  if ( !info.exists() )
  {
    if ( !create ) return {};
    if ( !QDir().mkpath( directory ) ) return QStringLiteral( "checkpoint_store_unavailable" );
    info.refresh();
  }
  if ( !info.isDir() ) return QStringLiteral( "checkpoint_store_path_refused" );
  return {};
}

QString checkFile( const QString &path )
{
  const QFileInfo info( path );
  if ( info.isSymLink() || ( info.exists() && !info.isFile() ) ) return QStringLiteral( "checkpoint_store_path_refused" );
  return {};
}

State readState( const QString &path, const Binding &binding )
{
  State state;
  if ( const QString error = checkFile( path ); !error.isEmpty() ) { state.error = error; return state; }
  const auto loaded = platform::sidecar::read( path.toUtf8().toStdString(), {}, Store::kMaxStoreBytes );
  if ( loaded.source == platform::sidecar::ReadSource::Missing ) return state;
  if ( loaded.source != platform::sidecar::ReadSource::Main )
  { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
  state.found = true;
  QJsonObject envelope;
  const QByteArray bytes( loaded.bytes.data(), static_cast<qsizetype>( loaded.bytes.size() ) );
  if ( !parseObject( bytes, envelope ).isEmpty() ) { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
  const QSet<QString> keys{ QStringLiteral( "schema" ), QStringLiteral( "projectId" ), QStringLiteral( "conversationId" ),
    QStringLiteral( "checkpoint" ), QStringLiteral( "checkpointSha256" ), QStringLiteral( "incompleteRunId" ), QStringLiteral( "executionRefs" ) };
  if ( !exactKeys( envelope, keys ) ) { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
  if ( envelope.value( QStringLiteral( "schema" ) ).toString() != QLatin1String( "sicnu.pi-conversation-store/1" ) )
  { state.error = QStringLiteral( "checkpoint_store_version_unsupported" ); return state; }
  if ( envelope.value( QStringLiteral( "projectId" ) ).toString() != binding.projectId
       || envelope.value( QStringLiteral( "conversationId" ) ).toString() != binding.conversationId )
  { state.error = QStringLiteral( "checkpoint_binding_mismatch" ); return state; }
  if ( !envelope.value( QStringLiteral( "incompleteRunId" ) ).isString()
       || !envelope.value( QStringLiteral( "checkpointSha256" ) ).isString() )
  { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
  state.interruptedRunId = envelope.value( QStringLiteral( "incompleteRunId" ) ).toString();
  if ( !state.interruptedRunId.isEmpty() && !identity( state.interruptedRunId ) )
  { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
  if ( !envelope.value( QStringLiteral( "executionRefs" ) ).isArray()
       || envelope.value( QStringLiteral( "executionRefs" ) ).toArray().size() > Store::kMaxExecutionRefs )
  { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
  QSet<QString> seenRefs;
  for ( const auto &value : envelope.value( QStringLiteral( "executionRefs" ) ).toArray() )
  {
    const QJsonObject object = value.toObject();
    if ( !value.isObject() || !exactKeys( object, { QStringLiteral( "kind" ), QStringLiteral( "nativeId" ), QStringLiteral( "jobId" ), QStringLiteral( "processBootId" ) } )
         || !object.value( QStringLiteral( "nativeId" ) ).isDouble() || !object.value( QStringLiteral( "jobId" ) ).isString()
         || !object.value( QStringLiteral( "processBootId" ) ).isString() )
    { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
    const QString kind = object.value( QStringLiteral( "kind" ) ).toString();
    const double nativeId = object.value( QStringLiteral( "nativeId" ) ).toDouble();
    if ( ( kind != QLatin1String( "task" ) && kind != QLatin1String( "pipeline" ) )
         || !std::isfinite( nativeId ) || nativeId < 1 || nativeId > 9007199254740991.0 || std::floor( nativeId ) != nativeId )
    { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
    const ExecutionRef ref{ kind == QLatin1String( "task" ) ? ExecutionRef::Kind::Task : ExecutionRef::Kind::Pipeline,
      static_cast<qint64>( nativeId ), object.value( QStringLiteral( "jobId" ) ).toString(), object.value( QStringLiteral( "processBootId" ) ).toString() };
    const QString refKey = kind + QLatin1Char( ':' ) + QString::number( ref.nativeId ) + QLatin1Char( ':' ) + ref.processBootId;
    if ( !validRef( ref ) || seenRefs.contains( refKey ) || state.interruptedRunId.isEmpty() )
    { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
    seenRefs.insert( refKey );
    state.executionRefs.append( ref );
  }
  state.checkpointSha256 = envelope.value( QStringLiteral( "checkpointSha256" ) ).toString();
  const auto payload = envelope.value( QStringLiteral( "checkpoint" ) );
  if ( payload.isNull() )
  {
    if ( !state.checkpointSha256.isEmpty() ) state.error = QStringLiteral( "checkpoint_store_corrupt" );
    return state;
  }
  if ( !payload.isObject() ) { state.error = QStringLiteral( "checkpoint_store_corrupt" ); return state; }
  state.error = validateCheckpoint( binding, QJsonDocument( payload.toObject() ).toJson( QJsonDocument::Compact ),
                                    state.checkpoint, state.checkpointJson );
  if ( state.error.isEmpty() && ( !digest( state.checkpointSha256 ) || sha256( state.checkpointJson ) != state.checkpointSha256 ) )
    state.error = QStringLiteral( "checkpoint_store_corrupt" );
  return state;
}

WriteResult publish( const QString &path, const Binding &binding, const State &state )
{
  const QJsonObject envelope{ { QStringLiteral( "schema" ), QStringLiteral( "sicnu.pi-conversation-store/1" ) },
    { QStringLiteral( "projectId" ), binding.projectId }, { QStringLiteral( "conversationId" ), binding.conversationId },
    { QStringLiteral( "checkpoint" ), state.checkpoint.isEmpty() ? QJsonValue( QJsonValue::Null ) : QJsonValue( state.checkpoint ) },
    { QStringLiteral( "checkpointSha256" ), state.checkpointSha256 }, { QStringLiteral( "incompleteRunId" ), state.interruptedRunId },
    { QStringLiteral( "executionRefs" ), refsToJson( state.executionRefs ) } };
  const QByteArray bytes = QJsonDocument( envelope ).toJson( QJsonDocument::Compact );
  if ( bytes.size() > Store::kMaxStoreBytes ) return { false, QStringLiteral( "checkpoint_too_large" ) };
  platform::sidecar::WriteRequest request;
  request.targetPath = path.toUtf8().toStdString();
  request.bytes.assign( bytes.constData(), static_cast<std::size_t>( bytes.size() ) );
  request.lastGoodSuffix.clear();
  request.maxBytes = Store::kMaxStoreBytes;
  const auto written = platform::sidecar::write( request );
  if ( written ) return { true, {} };
  if ( written.status == platform::sidecar::WriteStatus::VerifyFailed )
    return { true, QStringLiteral( "checkpoint_store_verify_failed" ) };
  return { false, QStringLiteral( "checkpoint_store_write_failed" ) };
}

template <typename Mutation>
WriteResult mutate( const QString &directory, const Binding &binding, Mutation mutation )
{
  if ( !validBinding( binding ) ) return { false, QStringLiteral( "checkpoint_store_invalid_binding" ) };
  if ( const QString error = checkDirectory( directory, true ); !error.isEmpty() ) return { false, error };
  const QString path = storagePath( directory, binding );
  if ( const QString error = checkFile( path ); !error.isEmpty() ) return { false, error };
  if ( const QString error = checkFile( path + QStringLiteral( ".lock" ) ); !error.isEmpty() ) return { false, error };
  QLockFile lock( path + QStringLiteral( ".lock" ) );
  if ( !lock.tryLock( 0 ) ) return { false, QStringLiteral( "checkpoint_store_busy" ) };
  State state = readState( path, binding );
  if ( !state.error.isEmpty() ) return { false, state.error };
  if ( const QString error = mutation( state ); !error.isEmpty() ) return { false, error };
  return publish( path, binding, state );
}
} // namespace

ConversationCheckpointStore::ConversationCheckpointStore( const QString &absoluteDirectory )
  : m_directory( QDir::cleanPath( absoluteDirectory ) )
{
}

ConversationCheckpointWriteResult ConversationCheckpointStore::saveCompleted(
  const ConversationCheckpointBinding &binding, const QByteArray &checkpointJson ) const
{
  if ( !validBinding( binding ) ) return { false, QStringLiteral( "checkpoint_store_invalid_binding" ) };
  QJsonObject checkpoint;
  QByteArray compact;
  if ( const QString error = validateCheckpoint( binding, checkpointJson, checkpoint, compact ); !error.isEmpty() ) return { false, error };
  return mutate( m_directory, binding, [&checkpoint, &compact]( State &state ) -> QString {
    if ( !state.interruptedRunId.isEmpty()
         && state.interruptedRunId != checkpoint.value( QStringLiteral( "completedRunId" ) ).toString() )
      return QStringLiteral( "checkpoint_stale_run" );
    state.checkpoint = checkpoint;
    state.checkpointJson = compact;
    state.checkpointSha256 = sha256( compact );
    state.interruptedRunId.clear();
    state.executionRefs.clear();
    return {};
  } );
}

ConversationCheckpointWriteResult ConversationCheckpointStore::markRunStarted(
  const ConversationCheckpointBinding &binding, const QString &runId ) const
{
  if ( !identity( runId ) ) return { false, QStringLiteral( "checkpoint_invalid_run" ) };
  return mutate( m_directory, binding, [&runId]( State &state ) -> QString {
    if ( !state.interruptedRunId.isEmpty() && state.interruptedRunId != runId ) return QStringLiteral( "checkpoint_interruption_pending" );
    if ( state.checkpoint.value( QStringLiteral( "completedRunId" ) ).toString() == runId ) return QStringLiteral( "checkpoint_run_reused" );
    if ( state.interruptedRunId.isEmpty() ) state.executionRefs.clear();
    state.interruptedRunId = runId;
    return {};
  } );
}

ConversationCheckpointWriteResult ConversationCheckpointStore::recordOwnedExecutions(
  const ConversationCheckpointBinding &binding, const QString &runId,
  const QList<ConversationExecutionReference> &refs ) const
{
  if ( !identity( runId ) ) return { false, QStringLiteral( "checkpoint_invalid_run" ) };
  if ( refs.size() > kMaxExecutionRefs ) return { false, QStringLiteral( "checkpoint_execution_refs_too_large" ) };
  for ( const auto &ref : refs ) if ( !validRef( ref ) ) return { false, QStringLiteral( "checkpoint_execution_ref_invalid" ) };
  return mutate( m_directory, binding, [&runId, &refs]( State &state ) -> QString {
    if ( state.interruptedRunId != runId ) return QStringLiteral( "checkpoint_stale_run" );
    for ( const auto &ref : refs )
    {
      auto found = std::find_if( state.executionRefs.begin(), state.executionRefs.end(), [&ref]( const ExecutionRef &old ) {
        return old.kind == ref.kind && old.nativeId == ref.nativeId && old.processBootId == ref.processBootId;
      } );
      if ( found == state.executionRefs.end() ) state.executionRefs.append( ref );
      else if ( !found->jobId.isEmpty() && !ref.jobId.isEmpty() && found->jobId != ref.jobId ) return QStringLiteral( "checkpoint_execution_identity_conflict" );
      else if ( !ref.jobId.isEmpty() ) found->jobId = ref.jobId;
    }
    if ( state.executionRefs.size() > Store::kMaxExecutionRefs ) return QStringLiteral( "checkpoint_execution_refs_too_large" );
    return {};
  } );
}

ConversationCheckpointWriteResult ConversationCheckpointStore::acknowledgeInterruption(
  const ConversationCheckpointBinding &binding, const QString &runId ) const
{
  if ( !identity( runId ) ) return { false, QStringLiteral( "checkpoint_invalid_run" ) };
  return mutate( m_directory, binding, [&runId]( State &state ) -> QString {
    if ( state.interruptedRunId != runId ) return QStringLiteral( "checkpoint_interruption_mismatch" );
    state.interruptedRunId.clear();
    state.executionRefs.clear();
    return {};
  } );
}

ConversationCheckpointLoadResult ConversationCheckpointStore::load( const ConversationCheckpointBinding &binding ) const
{
  if ( !validBinding( binding ) ) return { false, {}, {}, {}, {}, QStringLiteral( "checkpoint_store_invalid_binding" ) };
  if ( const QString error = checkDirectory( m_directory, false ); !error.isEmpty() ) return { false, {}, {}, {}, {}, error };
  const State state = readState( storagePath( m_directory, binding ), binding );
  if ( !state.error.isEmpty() ) return { state.found, {}, {}, {}, {}, state.error };
  return { state.found, state.checkpointJson, state.checkpointSha256, state.interruptedRunId, state.executionRefs, {} };
}

} // namespace sicnu::agent::runtime
