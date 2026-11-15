#include "pi_process.h"

#include "processing/framework/worker_process_guard.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <cmath>
#include <stdexcept>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef SICNU_PI_RECIPE_DIGEST
#define SICNU_PI_RECIPE_DIGEST ""
#endif

namespace sicnu::agent::runtime
{
namespace
{
constexpr qsizetype FrameLimit = 2 * 1024 * 1024;
constexpr qsizetype ResultLimit = 512 * 1024;
const QString NodeRelative = QStringLiteral( "libexec/sicnu-geo-rs/node/node.exe" );
const QString EntryRelative = QStringLiteral( "libexec/sicnu-geo-rs/pi-product/main.mjs" );
const QString SdkReceipt = QStringLiteral( "share/sicnu-geo-rs/pi-runtime.json" );
const QString ProductReceipt = QStringLiteral( "share/sicnu-geo-rs/pi-product.json" );
const QStringList SdkRoots{ QStringLiteral( "libexec/sicnu-geo-rs/node" ),
  QStringLiteral( "libexec/sicnu-geo-rs/pi-agent" ), QStringLiteral( "share/sicnu-geo-rs/pi" ) };
const QString ProductRoot = QStringLiteral( "libexec/sicnu-geo-rs/pi-product" );

bool identifier( const QString &value )
{
  if ( value.isEmpty() || value.toUtf8().size() > 256 ) return false;
  for ( const QChar c : value ) if ( c.unicode() < 0x20 || c.unicode() == 0x7f ) return false;
  return true;
}

bool hexHash( const QString &value )
{
  static const QRegularExpression pattern( QStringLiteral( "^[0-9a-f]{64}$" ) );
  return pattern.match( value ).hasMatch();
}

bool relativePath( const QString &path )
{
  if ( path.isEmpty() || path.contains( QLatin1Char( '\\' ) ) || path.contains( QLatin1Char( ':' ) ) || QDir::isAbsolutePath( path ) ) return false;
  for ( const QString &part : path.split( QLatin1Char( '/' ) ) )
    if ( part.isEmpty() || part == QLatin1String( "." ) || part == QLatin1String( ".." ) ) return false;
  return !path.contains( QChar::Null );
}

void noLink( const QString &path )
{
  const QFileInfo info( path );
  if ( info.isSymLink() ) throw std::runtime_error( "runtime_link" );
#ifdef Q_OS_WIN
  const DWORD attributes = GetFileAttributesW( reinterpret_cast<LPCWSTR>( path.utf16() ) );
  if ( attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_REPARSE_POINT ) )
    throw std::runtime_error( "runtime_link" );
#endif
  if ( !info.exists() ) throw std::runtime_error( "runtime_missing" );
}

void noLinkedAncestors( QString path )
{
  path = QDir::cleanPath( path );
  while ( true )
  {
    noLink( path );
    const QString parent = QFileInfo( path ).absolutePath();
    if ( parent == path ) break;
    path = parent;
  }
}

struct Check
{
  std::shared_ptr<std::atomic_bool> cancelled;
  QElapsedTimer elapsed;
  int timeout;
  void poll() const
  {
    if ( cancelled->load() ) throw std::runtime_error( "verification_cancelled" );
    if ( elapsed.elapsed() > timeout ) throw std::runtime_error( "verification_timeout" );
  }
};

QByteArray readSmall( const QString &path, Check &check, qint64 maximum = 64 * 1024 * 1024 )
{
  check.poll();
  noLinkedAncestors( path );
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) || file.size() > maximum ) throw std::runtime_error( "runtime_receipt_invalid" );
  return file.readAll();
}

QJsonObject object( const QByteArray &bytes )
{
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson( bytes, &error );
  if ( error.error != QJsonParseError::NoError || !document.isObject() ) throw std::runtime_error( "runtime_receipt_invalid" );
  return document.object();
}

QString hashBytes( const QByteArray &bytes )
{
  return QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

QString hashFile( const QString &path, Check &check )
{
  check.poll();
  noLink( path );
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) ) throw std::runtime_error( "runtime_missing" );
  QCryptographicHash hash( QCryptographicHash::Sha256 );
  while ( !file.atEnd() )
  {
    check.poll();
    const QByteArray chunk = file.read( 64 * 1024 );
    if ( chunk.isEmpty() && file.error() != QFile::NoError ) throw std::runtime_error( "runtime_read_failed" );
    hash.addData( chunk );
  }
  return QString::fromLatin1( hash.result().toHex() );
}

void inventory( const QString &prefix, const QStringList &roots, const QJsonArray &files, Check &check )
{
  if ( files.isEmpty() || files.size() > 200000 ) throw std::runtime_error( "runtime_inventory_invalid" );
  QHash<QString, QJsonObject> expected;
  QSet<QString> expectedDirectories;
  for ( const QString &root : roots ) expectedDirectories.insert( root );
  QSet<QString> folded;
  for ( const auto &item : files )
  {
    const auto entry = item.toObject();
    const QString path = entry.value( QStringLiteral( "path" ) ).toString();
    bool belongs = false;
    for ( const QString &root : roots ) belongs |= path.startsWith( root + QLatin1Char( '/' ) );
    const double size = entry.value( QStringLiteral( "size" ) ).toDouble( -1 );
    if ( !relativePath( path ) || !belongs || !hexHash( entry.value( QStringLiteral( "sha256" ) ).toString() )
         || !std::isfinite( size ) || size < 0 || size > 9007199254740991.0 || std::floor( size ) != size
         || folded.contains( path.toCaseFolded() ) ) throw std::runtime_error( "runtime_inventory_invalid" );
    folded.insert( path.toCaseFolded() );
    expected.insert( path, entry );
    QString parent = path.section( QLatin1Char( '/' ), 0, -2 );
    while ( !parent.isEmpty() )
    {
      expectedDirectories.insert( parent );
      if ( !parent.contains( QLatin1Char( '/' ) ) ) break;
      parent = parent.section( QLatin1Char( '/' ), 0, -2 );
    }
  }
  QSet<QString> actual;
  for ( const QString &root : roots )
  {
    const QString start = QDir( prefix ).filePath( root );
    noLinkedAncestors( start );
    QStringList pending{ start };
    while ( !pending.isEmpty() )
    {
      check.poll();
      const QString directory = pending.takeLast();
      noLink( directory );
      for ( const auto &entry : QDir( directory ).entryInfoList( QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot ) )
      {
        check.poll();
        noLink( entry.absoluteFilePath() );
        if ( entry.isDir() )
        {
          if ( !expectedDirectories.contains( QDir( prefix ).relativeFilePath( entry.absoluteFilePath() ) ) )
            throw std::runtime_error( "runtime_inventory_mismatch" );
          pending.append( entry.absoluteFilePath() );
          continue;
        }
        if ( !entry.isFile() ) throw std::runtime_error( "runtime_inventory_invalid" );
        const QString path = QDir( prefix ).relativeFilePath( entry.absoluteFilePath() );
        if ( !expected.contains( path ) || actual.contains( path ) ) throw std::runtime_error( "runtime_inventory_mismatch" );
        actual.insert( path );
        const auto wanted = expected.value( path );
        if ( entry.size() != static_cast<qint64>( wanted.value( QStringLiteral( "size" ) ).toDouble() )
             || hashFile( entry.absoluteFilePath(), check ) != wanted.value( QStringLiteral( "sha256" ) ).toString() )
          throw std::runtime_error( "runtime_integrity_failed" );
      }
    }
  }
  if ( actual.size() != expected.size() ) throw std::runtime_error( "runtime_inventory_mismatch" );
}

struct Verification
{
  QString error;
  QString sdkHash;
};

Verification verify( const QString &prefix, const std::shared_ptr<std::atomic_bool> &cancelled, int timeout )
{
  try
  {
    Check check{ cancelled, {}, timeout };
    check.elapsed.start();
    noLinkedAncestors( prefix );
    const auto sdkBytes = readSmall( QDir( prefix ).filePath( SdkReceipt ), check );
    const auto sdk = object( sdkBytes );
    const auto product = object( readSmall( QDir( prefix ).filePath( ProductReceipt ), check, 1024 * 1024 ) );
    const auto required = product.value( QStringLiteral( "requires_sdk" ) ).toObject();
    const QString pin = QString::fromLatin1( SICNU_PI_RECIPE_DIGEST );
    const auto recipes = sdk.value( QStringLiteral( "recipes" ) ).toObject();
    const QString recipeHash = hashBytes( QJsonDocument( recipes ).toJson( QJsonDocument::Compact ) );
    const QString sdkHash = hashBytes( sdkBytes );
    if ( sdk.value( QStringLiteral( "schema" ) ) != QLatin1String( "sicnu.pi-runtime/1" )
         || product.value( QStringLiteral( "schema" ) ) != QLatin1String( "sicnu.pi-product/1" )
         || product.value( QStringLiteral( "protocol_version" ) ).toInt() != 1
         || product.value( QStringLiteral( "entry" ) ) != EntryRelative
         || sdk.value( QStringLiteral( "target" ) ) != QLatin1String( "x86_64-pc-windows-msvc" )
         || sdk.value( QStringLiteral( "pi_version" ) ) != QLatin1String( "1.5.2" )
         || sdk.value( QStringLiteral( "node_version" ) ) != QLatin1String( "24.22.3" )
         || required.value( QStringLiteral( "target" ) ) != sdk.value( QStringLiteral( "target" ) )
         || required.value( QStringLiteral( "pi_version" ) ) != sdk.value( QStringLiteral( "pi_version" ) )
         || required.value( QStringLiteral( "node_version" ) ) != sdk.value( QStringLiteral( "node_version" ) )
         || !hexHash( pin ) || recipeHash != pin || required.value( QStringLiteral( "recipe_digest" ) ) != pin
         || required.value( QStringLiteral( "receipt_sha256" ) ) != sdkHash ) throw std::runtime_error( "runtime_recipe_mismatch" );
    for ( auto it = recipes.begin(); it != recipes.end(); ++it )
    {
      if ( relativePath( it.key() ) ) noLinkedAncestors( QDir( prefix ).filePath( SdkRoots[2] + QLatin1Char( '/' ) + it.key() ) );
      if ( !relativePath( it.key() ) || !hexHash( it.value().toString() )
           || hashFile( QDir( prefix ).filePath( SdkRoots[2] + QLatin1Char( '/' ) + it.key() ), check ) != it.value().toString() )
        throw std::runtime_error( "runtime_recipe_mismatch" );
    }
    const auto productFiles = product.value( QStringLiteral( "files" ) ).toArray();
    const auto sources = product.value( QStringLiteral( "sources" ) ).toArray();
    const QSet<QString> names{ QStringLiteral( "main.mjs" ), QStringLiteral( "protocol.mjs" ), QStringLiteral( "controlled-session.mjs" ) };
    QSet<QString> recorded;
    if ( sources.size() != 3 || productFiles.size() != 3 ) throw std::runtime_error( "runtime_product_invalid" );
    for ( const auto &source : sources )
    {
      const auto entry = source.toObject();
      const QString path = entry.value( QStringLiteral( "path" ) ).toString();
      const QString name = path.section( QLatin1Char( '/' ), -1 );
      if ( !names.contains( name ) || path != QStringLiteral( "pi/product-runtime/" ) + name || recorded.contains( name )
           || !hexHash( entry.value( QStringLiteral( "sha256" ) ).toString() ) ) throw std::runtime_error( "runtime_product_invalid" );
      recorded.insert( name );
      bool found = false;
      for ( const auto &file : productFiles )
        found |= file.toObject().value( QStringLiteral( "path" ) ) == ProductRoot + QLatin1Char( '/' ) + name
                 && file.toObject().value( QStringLiteral( "sha256" ) ) == entry.value( QStringLiteral( "sha256" ) );
      if ( !found ) throw std::runtime_error( "runtime_product_invalid" );
    }
    inventory( prefix, SdkRoots, sdk.value( QStringLiteral( "files" ) ).toArray(), check );
    inventory( prefix, { ProductRoot }, productFiles, check );
    if ( !QFileInfo( QDir( prefix ).filePath( NodeRelative ) ).isFile() ) throw std::runtime_error( "runtime_missing" );
    check.poll();
    return { {}, sdkHash };
  }
  catch ( const std::exception &error ) { return { QString::fromLatin1( error.what() ), {} }; }
  catch ( ... ) { return { QStringLiteral( "runtime_verification_failed" ), {} }; }
}
} // namespace

struct PiProcess::Private
{
  PiProcess *owner;
  State state = State::Idle;
  Config config;
  quint64 epoch = 0;
  quint64 sent = 0;
  quint64 received = 0;
  QString run;
  QString sdkHash;
  QSet<QString> usedRuns;
  QSet<QString> seenCalls;
  QSet<QString> pendingCalls;
  QHash<QString, QString> tools;
  QProcess *process = nullptr;
  std::unique_ptr<sicnu::processing::WorkerProcessGuard> guard;
  std::unique_ptr<QTemporaryDir> workspace;
  std::shared_ptr<std::atomic_bool> verificationCancel;
  QTimer timer;
  QByteArray stdoutBuffer;
  QByteArray stderrLine;
  QByteArray stderrRing;
  bool droppingStderrLine = false;
  bool gotClosed = false;
  bool stoppedBeforeInitialize = false;

  explicit Private( PiProcess *parent ) : owner( parent )
  {
    timer.setSingleShot( true );
    QObject::connect( &timer, &QTimer::timeout, owner, [this] {
      if ( state == State::Closing || state == State::Cancelling ) fail( QStringLiteral( "shutdown_timeout" ) );
      else if ( state == State::Verifying ) fail( QStringLiteral( "verification_timeout" ) );
      else fail( QStringLiteral( "handshake_timeout" ) );
    } );
  }

  void setState( State next )
  {
    state = next;
    emit owner->stateChanged( next );
  }

  QString redact( QString text ) const
  {
    const QString secret = QString::fromUtf8( config.apiKey );
    if ( !secret.isEmpty() )
    {
      text.replace( secret, QStringLiteral( "[redacted]" ) );
      const QByteArray encoded = QJsonDocument( QJsonArray{ secret } ).toJson( QJsonDocument::Compact );
      text.replace( QString::fromUtf8( encoded.mid( 2, encoded.size() - 4 ) ), QStringLiteral( "[redacted]" ) );
    }
    return text;
  }

  QJsonValue safe( const QJsonValue &value ) const
  {
    if ( value.isString() ) return redact( value.toString() );
    if ( value.isArray() )
    {
      QJsonArray result;
      for ( const auto &item : value.toArray() ) result.append( safe( item ) );
      return result;
    }
    if ( value.isObject() )
    {
      QJsonObject result;
      const auto input = value.toObject();
      for ( auto it = input.begin(); it != input.end(); ++it ) result.insert( it.key(), safe( it.value() ) );
      return result;
    }
    return value;
  }

  void readStderr( bool flush = false )
  {
    if ( !process ) return;
    process->setReadChannel( QProcess::StandardError );
    while ( process->bytesAvailable() > 0 )
    {
      const QByteArray chunk = process->read( 64 * 1024 );
      for ( const char byte : chunk )
      {
        if ( byte == '\n' )
        {
          const QString line = droppingStderrLine ? QStringLiteral( "[stderr line exceeded limit]" ) : redact( QString::fromUtf8( stderrLine ) );
          stderrRing += line.toUtf8() + '\n';
          if ( stderrRing.size() > 256 * 1024 ) stderrRing.remove( 0, stderrRing.size() - 256 * 1024 );
          stderrLine.clear();
          droppingStderrLine = false;
        }
        else if ( !droppingStderrLine )
        {
          stderrLine += byte;
          if ( stderrLine.size() > 64 * 1024 ) { stderrLine.clear(); droppingStderrLine = true; }
        }
      }
    }
    if ( flush && ( !stderrLine.isEmpty() || droppingStderrLine ) )
    {
      stderrRing += ( droppingStderrLine ? QByteArray( "[stderr line exceeded limit]" ) : redact( QString::fromUtf8( stderrLine ) ).toUtf8() );
      if ( stderrRing.size() > 256 * 1024 ) stderrRing.remove( 0, stderrRing.size() - 256 * 1024 );
      stderrLine.clear();
    }
    process->setReadChannel( QProcess::StandardOutput );
  }

  bool send( const QString &type, const QString &runId, QJsonObject payload = {} )
  {
    if ( !process || process->state() != QProcess::Running || process->bytesToWrite() > 4 * FrameLimit ) return false;
    payload.insert( QStringLiteral( "protocol" ), QStringLiteral( "sicnu.pi/1" ) );
    payload.insert( QStringLiteral( "sessionId" ), config.sessionId );
    payload.insert( QStringLiteral( "generation" ), config.generation );
    payload.insert( QStringLiteral( "runId" ), runId );
    payload.insert( QStringLiteral( "seq" ), static_cast<double>( sent + 1 ) );
    payload.insert( QStringLiteral( "type" ), type );
    QByteArray frame = QJsonDocument( payload ).toJson( QJsonDocument::Compact );
    if ( frame.size() > FrameLimit || sent >= 9007199254740991ULL ) return false;
    frame += '\n';
    const qint64 wanted = frame.size();
    const qint64 written = process->write( frame );
    frame.fill( '\0' );
    if ( written != wanted ) return false;
    ++sent;
    return true;
  }

  void fail( const QString &code )
  {
    if ( state == State::Failed || state == State::Closed ) return;
    timer.stop();
    if ( verificationCancel ) verificationCancel->store( true );
    setState( State::Failed );
    emit owner->failed( code, QStringLiteral( "The managed Pi process could not complete its verified session." ) );
    if ( process && process->state() != QProcess::NotRunning ) process->kill();
    else finish();
  }

  void finish()
  {
    timer.stop();
    readStderr( true );
    guard.reset(); // Windows: also terminates descendants after leader exit.
    workspace.reset();
    config.apiKey.fill( '\0' );
    config.apiKey.clear();
    pendingCalls.clear();
    setState( State::Closed );
    emit owner->closed();
  }

  void frame( const QByteArray &bytes )
  {
    if ( state == State::Failed || state == State::Closed ) return;
    QStringDecoder decoder( QStringDecoder::Utf8 );
    const QString decoded = decoder.decode( bytes );
    Q_UNUSED( decoded );
    if ( decoder.hasError() ) { fail( QStringLiteral( "protocol_invalid_utf8" ) ); return; }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson( bytes, &error );
    if ( error.error != QJsonParseError::NoError || !document.isObject() ) { fail( QStringLiteral( "protocol_invalid_frame" ) ); return; }
    const auto event = document.object();
    const QString type = event.value( QStringLiteral( "type" ) ).toString();
    const QString runId = event.value( QStringLiteral( "runId" ) ).toString();
    const double seq = event.value( QStringLiteral( "seq" ) ).toDouble( -1 );
    if ( event.value( QStringLiteral( "protocol" ) ) != QLatin1String( "sicnu.pi/1" )
         || event.value( QStringLiteral( "sessionId" ) ).toString() != config.sessionId
         || event.value( QStringLiteral( "generation" ) ).toString() != config.generation
         || !event.value( QStringLiteral( "runId" ) ).isString()
         || !std::isfinite( seq ) || seq != static_cast<double>( received + 1 ) || seq > 9007199254740991.0 )
    { fail( QStringLiteral( "protocol_identity_mismatch" ) ); return; }
    QSet<QString> allowed{ QStringLiteral( "protocol" ), QStringLiteral( "sessionId" ), QStringLiteral( "generation" ), QStringLiteral( "runId" ), QStringLiteral( "seq" ), QStringLiteral( "type" ) };
    const QHash<QString, QStringList> fields{
      { QStringLiteral( "ready" ), { QStringLiteral( "nodeVersion" ), QStringLiteral( "piVersion" ), QStringLiteral( "sdkReceiptSha256" ), QStringLiteral( "toolRevision" ) } },
      { QStringLiteral( "content" ), { QStringLiteral( "text" ) } }, { QStringLiteral( "reasoning" ), { QStringLiteral( "text" ) } },
      { QStringLiteral( "tool.request" ), { QStringLiteral( "callId" ), QStringLiteral( "canonicalId" ), QStringLiteral( "name" ), QStringLiteral( "arguments" ), QStringLiteral( "toolRevision" ) } },
      { QStringLiteral( "tool.cancel" ), { QStringLiteral( "callId" ) } },
      { QStringLiteral( "usage" ), { QStringLiteral( "usage" ), QStringLiteral( "cumulativeUsage" ) } },
      { QStringLiteral( "compaction" ), { QStringLiteral( "stage" ), QStringLiteral( "reason" ) } },
      { QStringLiteral( "settled" ), { QStringLiteral( "aborted" ), QStringLiteral( "status" ), QStringLiteral( "errorCode" ) } },
      { QStringLiteral( "error" ), { QStringLiteral( "code" ), QStringLiteral( "message" ) } },
      { QStringLiteral( "closed" ), { QStringLiteral( "reason" ) } }
    };
    if ( !fields.contains( type ) ) { fail( QStringLiteral( "protocol_unknown_event" ) ); return; }
    for ( const QString &field : fields.value( type ) ) allowed.insert( field );
    for ( const QString &key : event.keys() ) if ( !allowed.contains( key ) ) { fail( QStringLiteral( "protocol_schema_mismatch" ) ); return; }
    ++received;
    if ( type == QLatin1String( "ready" ) )
    {
      if ( state != State::Starting || !runId.isEmpty()
           || event.value( QStringLiteral( "nodeVersion" ) ) != QLatin1String( "24.22.3" )
           || event.value( QStringLiteral( "piVersion" ) ) != QLatin1String( "1.5.2" )
           || event.value( QStringLiteral( "sdkReceiptSha256" ) ) != sdkHash
           || event.value( QStringLiteral( "toolRevision" ) ) != config.toolRevision )
      { fail( QStringLiteral( "protocol_handshake_mismatch" ) ); return; }
      timer.stop();
      setState( State::Ready );
      emit owner->ready( event );
    }
    else if ( type == QLatin1String( "closed" ) )
    {
      if ( state != State::Closing || !runId.isEmpty() || !event.value( QStringLiteral( "reason" ) ).isString() )
      { fail( QStringLiteral( "protocol_unexpected_close" ) ); return; }
      gotClosed = true;
      process->closeWriteChannel();
    }
    else
    {
      if ( type == QLatin1String( "error" ) )
      {
        if ( runId != run || !event.value( QStringLiteral( "code" ) ).isString() || !event.value( QStringLiteral( "message" ) ).isString() )
        { fail( QStringLiteral( "protocol_schema_mismatch" ) ); return; }
        emit owner->eventReceived( safe( event ).toObject() );
        fail( QStringLiteral( "sidecar_error" ) );
        return;
      }
      if ( run.isEmpty() || runId != run || ( state != State::Running && state != State::Cancelling && state != State::Closing ) )
      { fail( QStringLiteral( "protocol_run_mismatch" ) ); return; }
      if ( type == QLatin1String( "tool.request" ) )
      {
        const QString call = event.value( QStringLiteral( "callId" ) ).toString();
        const QString name = event.value( QStringLiteral( "name" ) ).toString();
        if ( state != State::Running || !identifier( call ) || seenCalls.contains( call ) || pendingCalls.size() >= 16 || seenCalls.size() >= 32
             || !tools.contains( name ) || event.value( QStringLiteral( "canonicalId" ) ).toString() != tools.value( name )
             || event.value( QStringLiteral( "toolRevision" ) ).toString() != config.toolRevision
             || !event.value( QStringLiteral( "arguments" ) ).isObject()
             || QJsonDocument( event.value( QStringLiteral( "arguments" ) ).toObject() ).toJson( QJsonDocument::Compact ).size() > 1024 * 1024 )
        { fail( QStringLiteral( "protocol_tool_mismatch" ) ); return; }
        seenCalls.insert( call );
        pendingCalls.insert( call );
      }
      else if ( type == QLatin1String( "tool.cancel" ) )
      {
        if ( !seenCalls.contains( event.value( QStringLiteral( "callId" ) ).toString() ) ) { fail( QStringLiteral( "protocol_call_mismatch" ) ); return; }
      }
      else if ( type == QLatin1String( "content" ) || type == QLatin1String( "reasoning" ) )
      {
        if ( !event.value( QStringLiteral( "text" ) ).isString() ) { fail( QStringLiteral( "protocol_schema_mismatch" ) ); return; }
      }
      else if ( type == QLatin1String( "usage" ) )
      {
        for ( const QString &field : { QStringLiteral( "usage" ), QStringLiteral( "cumulativeUsage" ) } )
        {
          if ( !event.value( field ).isObject() ) { fail( QStringLiteral( "protocol_schema_mismatch" ) ); return; }
          const auto usage = event.value( field ).toObject();
          if ( usage.size() != 5 ) { fail( QStringLiteral( "protocol_schema_mismatch" ) ); return; }
          for ( const QString &key : { QStringLiteral( "input" ), QStringLiteral( "output" ), QStringLiteral( "cacheRead" ), QStringLiteral( "cacheWrite" ), QStringLiteral( "totalTokens" ) } )
          {
            const double value = usage.value( key ).toDouble( -1 );
            if ( value < 0 || !std::isfinite( value ) || std::floor( value ) != value || value > 9007199254740991.0 )
            { fail( QStringLiteral( "protocol_schema_mismatch" ) ); return; }
          }
        }
      }
      else if ( type == QLatin1String( "compaction" ) )
      {
        const QString stage = event.value( QStringLiteral( "stage" ) ).toString();
        if ( !QStringList{ QStringLiteral( "started" ), QStringLiteral( "completed" ), QStringLiteral( "failed" ) }.contains( stage )
             || !event.value( QStringLiteral( "reason" ) ).isString() ) { fail( QStringLiteral( "protocol_schema_mismatch" ) ); return; }
      }
      else if ( type == QLatin1String( "settled" ) )
      {
        const QString status = event.value( QStringLiteral( "status" ) ).toString();
        if ( !pendingCalls.isEmpty() || !event.value( QStringLiteral( "aborted" ) ).isBool()
             || !QStringList{ QStringLiteral( "completed" ), QStringLiteral( "cancelled" ), QStringLiteral( "failed" ) }.contains( status )
             || ( event.contains( QStringLiteral( "errorCode" ) ) && !event.value( QStringLiteral( "errorCode" ) ).isString() ) )
        { fail( QStringLiteral( "protocol_premature_settled" ) ); return; }
        run.clear();
        if ( state != State::Closing ) { timer.stop(); setState( State::Ready ); }
      }
    }
    emit owner->eventReceived( safe( event ).toObject() );
  }

  void readStdout()
  {
    if ( !process ) return;
    process->setReadChannel( QProcess::StandardOutput );
    while ( process->bytesAvailable() > 0 )
    {
      stdoutBuffer += process->read( 64 * 1024 );
      qsizetype newline;
      while ( ( newline = stdoutBuffer.indexOf( '\n' ) ) >= 0 )
      {
        if ( newline > FrameLimit ) { fail( QStringLiteral( "protocol_frame_limit" ) ); return; }
        const QByteArray next = stdoutBuffer.left( newline );
        stdoutBuffer.remove( 0, newline + 1 );
        frame( next );
        if ( state == State::Closed || state == State::Failed ) return;
      }
      if ( stdoutBuffer.size() > FrameLimit ) { fail( QStringLiteral( "protocol_frame_limit" ) ); return; }
    }
  }

  void launch( const Verification &verified )
  {
    sdkHash = verified.sdkHash;
    workspace = std::make_unique<QTemporaryDir>( QDir::tempPath() + QStringLiteral( "/sicnu-pi-owned-XXXXXX" ) );
    if ( !workspace->isValid() ) { fail( QStringLiteral( "workspace_unavailable" ) ); return; }
    const QString cwd = QDir( workspace->path() ).filePath( QStringLiteral( "work" ) );
    if ( !QDir().mkpath( cwd ) ) { fail( QStringLiteral( "workspace_unavailable" ) ); return; }
    const QString node = QDir( config.prefixPath ).filePath( NodeRelative );
    QProcessEnvironment environment;
    const auto inherited = QProcessEnvironment::systemEnvironment();
    for ( const QString &key : { QStringLiteral( "SystemRoot" ), QStringLiteral( "WINDIR" ), QStringLiteral( "SYSTEMDRIVE" ), QStringLiteral( "COMSPEC" ), QStringLiteral( "PATHEXT" ) } )
      if ( inherited.contains( key ) ) environment.insert( key, inherited.value( key ) );
    for ( const QString &key : { QStringLiteral( "HOME" ), QStringLiteral( "USERPROFILE" ), QStringLiteral( "APPDATA" ), QStringLiteral( "LOCALAPPDATA" ), QStringLiteral( "XDG_CONFIG_HOME" ), QStringLiteral( "TEMP" ), QStringLiteral( "TMP" ) } )
      environment.insert( key, workspace->path() );
    environment.insert( QStringLiteral( "PATH" ), QFileInfo( node ).absolutePath() );
    environment.insert( QStringLiteral( "PI_OFFLINE" ), QStringLiteral( "1" ) );
    environment.insert( QStringLiteral( "PI_TELEMETRY" ), QStringLiteral( "0" ) );
    process = new QProcess( owner );
    process->setWorkingDirectory( cwd );
    process->setProcessEnvironment( environment );
    process->setProcessChannelMode( QProcess::SeparateChannels );
    guard = std::make_unique<sicnu::processing::WorkerProcessGuard>();
    guard->attach( *process );
    const quint64 current = epoch;
    QObject::connect( process, &QProcess::started, owner, [this, current, cwd] {
      if ( epoch != current || state != State::Starting ) { process->kill(); return; }
      if ( !guard->armAfterStart( *process, 1024ULL * 1024 * 1024 ) ) { fail( QStringLiteral( "containment_unavailable" ) ); return; }
      auto profile = config.profile;
      profile.insert( QStringLiteral( "apiKey" ), QString::fromUtf8( config.apiKey ) );
      if ( !send( QStringLiteral( "initialize" ), {}, { { QStringLiteral( "cwd" ), cwd }, { QStringLiteral( "profile" ), profile },
           { QStringLiteral( "tools" ), config.tools }, { QStringLiteral( "systemPrompt" ), config.systemPrompt },
           { QStringLiteral( "toolRevision" ), config.toolRevision }, { QStringLiteral( "sdkReceiptSha256" ), sdkHash } } ) )
        fail( QStringLiteral( "initialize_write_failed" ) );
    } );
    QObject::connect( process, &QProcess::readyReadStandardOutput, owner, [this] { readStdout(); } );
    QObject::connect( process, &QProcess::readyReadStandardError, owner, [this] { readStderr(); } );
    QObject::connect( process, &QProcess::errorOccurred, owner, [this]( QProcess::ProcessError error ) {
      if ( error == QProcess::FailedToStart )
      {
        if ( state == State::Closing && stoppedBeforeInitialize ) finish();
        else fail( QStringLiteral( "process_start_failed" ) );
      }
    } );
    QObject::connect( process, qOverload<int, QProcess::ExitStatus>( &QProcess::finished ), owner, [this]( int code, QProcess::ExitStatus status ) {
      readStdout();
      readStderr( true );
      if ( state != State::Failed && !( state == State::Closing && ( stoppedBeforeInitialize
           || ( gotClosed && code == 0 && status == QProcess::NormalExit && stdoutBuffer.isEmpty() ) ) ) )
      { fail( QStringLiteral( "process_unexpected_exit" ) ); return; }
      if ( state != State::Closed ) finish();
    } );
    setState( State::Starting );
    timer.start( config.readyTimeoutMs );
    process->start( node, { QStringLiteral( "--max-old-space-size=512" ), QDir( config.prefixPath ).filePath( EntryRelative ) }, QIODevice::ReadWrite );
  }
};

PiProcess::PiProcess( QObject *parent ) : QObject( parent ), d( std::make_unique<Private>( this ) ) {}
PiProcess::~PiProcess()
{
  ++d->epoch;
  d->timer.stop();
  if ( d->verificationCancel ) d->verificationCancel->store( true );
  if ( d->process )
  {
    d->process->disconnect( this );
    if ( d->guard ) d->guard->terminateTree( *d->process, 100, 1000 );
    else { d->process->kill(); d->process->waitForFinished( 1000 ); }
  }
  d->config.apiKey.fill( '\0' );
}

PiProcess::State PiProcess::state() const { return d->state; }
QString PiProcess::stderrTail() const { return QString::fromUtf8( d->stderrRing ); }
QString PiProcess::toolRevision( const QJsonArray &tools ) { return hashBytes( QJsonDocument( tools ).toJson( QJsonDocument::Compact ) ); }

void PiProcess::start( const Config &config )
{
  if ( d->state != State::Idle && d->state != State::Closed ) return;
  if ( d->process ) { delete d->process; d->process = nullptr; }
  d->config = config;
  ++d->epoch;
  d->sent = d->received = 0;
  d->run.clear(); d->usedRuns.clear(); d->seenCalls.clear(); d->pendingCalls.clear(); d->tools.clear();
  d->stdoutBuffer.clear(); d->stderrLine.clear(); d->stderrRing.clear();
  d->droppingStderrLine = d->gotClosed = d->stoppedBeforeInitialize = false;
  if ( !QDir::isAbsolutePath( config.prefixPath ) || config.prefixPath.startsWith( QLatin1String( "//" ) ) || config.prefixPath.startsWith( QLatin1String( "\\\\" ) )
       || !identifier( config.sessionId ) || !identifier( config.generation ) || config.apiKey.isEmpty() || config.apiKey.size() > 8192
       || config.apiKey.contains( '\n' ) || config.apiKey.contains( '\r' ) || config.apiKey.contains( '\0' )
       || config.profile.contains( QStringLiteral( "apiKey" ) ) || config.systemPrompt.isEmpty() || config.systemPrompt.toUtf8().size() > 64 * 1024
       || config.tools.size() > 256 || config.profile.size() != 4
       || config.profile.value( QStringLiteral( "baseUrl" ) ).toString().isEmpty() || config.profile.value( QStringLiteral( "baseUrl" ) ).toString().toUtf8().size() > 16384
       || config.profile.value( QStringLiteral( "model" ) ).toString().isEmpty() || config.profile.value( QStringLiteral( "model" ) ).toString().toUtf8().size() > 256
       || config.verifyTimeoutMs <= 0 || config.verifyTimeoutMs > 900000 || config.readyTimeoutMs <= 0 || config.readyTimeoutMs > 120000
       || config.closeTimeoutMs <= 0 || config.closeTimeoutMs > 3000 )
  { d->fail( QStringLiteral( "invalid_configuration" ) ); return; }
  for ( const QString &field : { QStringLiteral( "contextWindow" ), QStringLiteral( "maxTokens" ) } )
  {
    const double value = config.profile.value( field ).toDouble( -1 );
    if ( !std::isfinite( value ) || value <= 0 || value > 9007199254740991.0 || std::floor( value ) != value )
    { d->fail( QStringLiteral( "invalid_configuration" ) ); return; }
  }
  if ( d->config.toolRevision.isEmpty() ) d->config.toolRevision = toolRevision( config.tools );
  if ( !identifier( d->config.toolRevision ) ) { d->fail( QStringLiteral( "invalid_tool_revision" ) ); return; }
  QSet<QString> canonical;
  for ( const auto &value : config.tools )
  {
    const auto tool = value.toObject();
    const QString name = tool.value( QStringLiteral( "name" ) ).toString();
    const QString id = tool.value( QStringLiteral( "canonicalId" ) ).toString();
    static const QRegularExpression toolName( QStringLiteral( "^[A-Za-z0-9_-]{1,64}$" ) );
    if ( tool.size() != 4 || !toolName.match( name ).hasMatch() || !identifier( id ) || d->tools.contains( name ) || canonical.contains( id )
         || !tool.value( QStringLiteral( "parameters" ) ).isObject() || tool.value( QStringLiteral( "parameters" ) ).toObject().value( QStringLiteral( "type" ) ).toString() != QLatin1String( "object" )
         || !tool.value( QStringLiteral( "description" ) ).isString() || tool.value( QStringLiteral( "description" ) ).toString().toUtf8().size() > 64 * 1024 )
    { d->fail( QStringLiteral( "invalid_tool_catalog" ) ); return; }
    d->tools.insert( name, id ); canonical.insert( id );
  }
  d->verificationCancel = std::make_shared<std::atomic_bool>( false );
  const auto cancel = d->verificationCancel;
  const quint64 epoch = d->epoch;
  auto *watcher = new QFutureWatcher<Verification>( this );
  connect( watcher, &QFutureWatcher<Verification>::finished, this, [this, watcher, epoch] {
    const auto result = watcher->result();
    watcher->deleteLater();
    if ( d->epoch != epoch || d->state != State::Verifying ) return;
    if ( !result.error.isEmpty() ) { d->fail( result.error ); return; }
    d->launch( result );
  } );
  d->setState( State::Verifying );
  d->timer.start( config.verifyTimeoutMs );
  watcher->setFuture( QtConcurrent::run( [prefix = config.prefixPath, cancel, timeout = config.verifyTimeoutMs] { return verify( prefix, cancel, timeout ); } ) );
}

bool PiProcess::sendPrompt( const QString &runId, const QString &text )
{
  if ( d->state != State::Ready || !identifier( runId ) || d->usedRuns.contains( runId ) || text.isEmpty() || text.toUtf8().size() > 64 * 1024 ) return false;
  if ( !d->send( QStringLiteral( "prompt" ), runId, { { QStringLiteral( "text" ), text } } ) ) { d->fail( QStringLiteral( "prompt_write_failed" ) ); return false; }
  d->run = runId; d->usedRuns.insert( runId ); d->seenCalls.clear(); d->pendingCalls.clear();
  d->setState( State::Running );
  return true;
}

bool PiProcess::completeTool( const QString &runId, const QString &callId, const QJsonObject &result )
{
  if ( runId != d->run || !d->pendingCalls.contains( callId )
       || ( d->state != State::Running && d->state != State::Cancelling && d->state != State::Closing )
       || QJsonDocument( result ).toJson( QJsonDocument::Compact ).size() > ResultLimit ) return false;
  if ( !d->send( QStringLiteral( "tool.result" ), runId, { { QStringLiteral( "callId" ), callId }, { QStringLiteral( "result" ), result } } ) )
  { d->fail( QStringLiteral( "tool_result_write_failed" ) ); return false; }
  d->pendingCalls.remove( callId );
  return true;
}

void PiProcess::cancel()
{
  if ( d->state == State::Verifying || d->state == State::Starting ) { close(); return; }
  if ( d->state != State::Running ) return;
  if ( !d->send( QStringLiteral( "cancel" ), d->run ) ) { d->fail( QStringLiteral( "cancel_write_failed" ) ); return; }
  d->setState( State::Cancelling );
  d->timer.start( d->config.closeTimeoutMs );
}

void PiProcess::close()
{
  if ( d->state == State::Closed || d->state == State::Closing ) return;
  if ( d->verificationCancel ) d->verificationCancel->store( true );
  if ( !d->process || d->process->state() == QProcess::NotRunning )
  { ++d->epoch; d->finish(); return; }
  if ( d->process->state() == QProcess::Starting )
  {
    d->stoppedBeforeInitialize = true;
    d->setState( State::Closing );
    d->timer.start( d->config.closeTimeoutMs );
    d->process->kill();
    return;
  }
  if ( !d->run.isEmpty() ) d->send( QStringLiteral( "cancel" ), d->run );
  if ( !d->send( QStringLiteral( "shutdown" ), {} ) ) { d->fail( QStringLiteral( "shutdown_write_failed" ) ); return; }
  d->setState( State::Closing );
  d->timer.start( d->config.closeTimeoutMs );
}

} // namespace sicnu::agent::runtime
