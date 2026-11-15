#include <catch2/catch_test_macros.hpp>

#include "agent/runtime/pi_process.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <functional>

using sicnu::agent::runtime::PiProcess;

namespace
{
void application()
{
  if ( QCoreApplication::instance() ) return;
  static int argc = 1;
  static char name[] = "test_pi_process";
  static char *argv[] = { name, nullptr };
  new QCoreApplication( argc, argv );
}

bool until( const std::function<bool()> &predicate, int timeout = 20000 )
{
  QElapsedTimer timer;
  timer.start();
  while ( !predicate() && timer.elapsed() < timeout )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    QThread::msleep( 5 );
  }
  return predicate();
}

QByteArray bytes( const QString &path )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  return file.readAll();
}
QString hash( const QByteArray &value )
{
  return QString::fromLatin1( QCryptographicHash::hash( value, QCryptographicHash::Sha256 ).toHex() );
}
void write( const QString &path, const QByteArray &value )
{
  REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  REQUIRE( file.write( value ) == value.size() );
}

const QByteArray fixture = R"JS(import {createInterface} from 'node:readline';
import {writeFileSync} from 'node:fs';
let init, mode, run='', call='', cancelled=false, seq=0;
function send(type, fields={}, runId=run) {
  const event={protocol:'sicnu.pi/1',sessionId:init.sessionId,generation:init.generation,runId,seq:++seq,type,...fields};
  if(mode==='wrong-session')event.sessionId='foreign';
  if(mode==='wrong-generation')event.generation='stale';
  if(mode==='wrong-seq')event.seq++;
  process.stdout.write(JSON.stringify(event)+'\n');
}
function settled(){send('settled',{aborted:cancelled,status:cancelled?'cancelled':'completed'});run='';}
const rl=createInterface({input:process.stdin});
rl.on('line',line=>{
 const m=JSON.parse(line);
 if(m.type==='initialize'){
  init=m;const options=JSON.parse(m.systemPrompt);mode=options.mode;
  writeFileSync(options.marker,'initialize received; no secret stored');
  if(process.env.NODE_OPTIONS||process.env.NODE_PATH||process.env.OPENAI_API_KEY||process.argv.includes(m.profile.apiKey)||process.cwd()!==m.cwd||process.env.HOME===options.forbiddenHome)process.exit(19);
  if(mode==='eof')process.exit(0);
  if(mode==='never-ready')return;
  if(mode==='malformed'){process.stdout.write('{not json}\n');return;}
  if(mode==='huge'){process.stdout.write('x'.repeat(2*1024*1024+1));return;}
  if(mode==='stderr'){
   process.stderr.write('prefix '+m.profile.apiKey.slice(0,4));
   setTimeout(()=>process.stderr.write(m.profile.apiKey.slice(4)+' suffix\n'+'x'.repeat(70000)+'\n'),5);
  }
  send('ready',{nodeVersion:mode==='bad-ready'?'0.0.0':'24.22.3',piVersion:'1.5.2',sdkReceiptSha256:m.sdkReceiptSha256,toolRevision:m.toolRevision},'');
 }else if(m.type==='prompt'){
  run=m.runId;
  if(mode==='invalid-utf8'){
   const e={protocol:'sicnu.pi/1',sessionId:init.sessionId,generation:init.generation,runId:run,seq:++seq,type:'content',text:'BAD_BYTE'};
   const b=Buffer.from(JSON.stringify(e)+'\n');const at=b.indexOf('BAD_BYTE');b[at]=255;process.stdout.write(b);return;
  }
  if(mode==='stall')return;
  if(mode==='tool'||mode==='cancel-tool'||mode==='wrong-run'||mode==='wrong-call'){
   call='call-1';const t=init.tools[0];send('tool.request',{callId:call,name:t.name,canonicalId:t.canonicalId,arguments:{value:7},toolRevision:init.toolRevision},mode==='wrong-run'?'stale':run);
  }else {send('content',{text:'fixture text'});settled();}
 }else if(m.type==='tool.result'){
  send('content',{text:'asset:'+m.result.assetId});settled();
 }else if(m.type==='cancel'){
  cancelled=true;if(call)send('tool.cancel',{callId:mode==='wrong-call'?'unknown':call});else settled();
 }else if(m.type==='shutdown'){
  if(mode==='ignore-close')return;
  send('closed',{reason:'shutdown'},'');setImmediate(()=>process.exit(0));
 }
});
)JS";

struct Prefix
{
  QTemporaryDir directory;
  QString prefix;
  QString marker;
  QJsonObject sdk;
  QJsonArray sdkFiles;

  Prefix()
  {
    REQUIRE( directory.isValid() );
    prefix = directory.filePath( QStringLiteral( "安装 Prefix" ) );
    marker = directory.filePath( QStringLiteral( "initialized.txt" ) );
    const QString node = qEnvironmentVariable( "SICNU_TEST_PI_NODE" );
    REQUIRE( QDir::isAbsolutePath( node ) );
    REQUIRE( QFileInfo( node ).fileName() == QStringLiteral( "node.exe" ) );
    const QString stage = QDir::cleanPath( QFileInfo( node ).absolutePath() + QStringLiteral( "/../../.." ) );
    const auto original = QJsonDocument::fromJson( bytes( QDir( stage ).filePath( QStringLiteral( "share/sicnu-geo-rs/pi-runtime.json" ) ) ) ).object();
    REQUIRE( original.value( QStringLiteral( "node_version" ) ).toString() == QStringLiteral( "24.22.3" ) );
    auto add = [&]( const QString &path, const QByteArray &data ) {
      write( QDir( prefix ).filePath( path ), data );
      sdkFiles.append( QJsonObject{ { QStringLiteral( "path" ), path }, { QStringLiteral( "size" ), static_cast<double>( data.size() ) }, { QStringLiteral( "sha256" ), hash( data ) } } );
    };
    add( QStringLiteral( "libexec/sicnu-geo-rs/node/node.exe" ), bytes( node ) );
    const auto recipes = original.value( QStringLiteral( "recipes" ) ).toObject();
    for ( auto it = recipes.begin(); it != recipes.end(); ++it )
    {
      const QString relative = QStringLiteral( "share/sicnu-geo-rs/pi/" ) + it.key();
      add( relative, bytes( QDir( stage ).filePath( relative ) ) );
    }
    REQUIRE( QDir().mkpath( QDir( prefix ).filePath( QStringLiteral( "libexec/sicnu-geo-rs/pi-agent" ) ) ) );
    sdk = { { QStringLiteral( "schema" ), QStringLiteral( "sicnu.pi-runtime/1" ) },
      { QStringLiteral( "target" ), QStringLiteral( "x86_64-pc-windows-msvc" ) },
      { QStringLiteral( "pi_version" ), QStringLiteral( "1.5.2" ) }, { QStringLiteral( "node_version" ), QStringLiteral( "24.22.3" ) },
      { QStringLiteral( "recipes" ), recipes }, { QStringLiteral( "files" ), sdkFiles } };
    receipts();
  }

  void receipts()
  {
    const QByteArray sdkBytes = QJsonDocument( sdk ).toJson( QJsonDocument::Compact );
    write( QDir( prefix ).filePath( QStringLiteral( "share/sicnu-geo-rs/pi-runtime.json" ) ), sdkBytes );
    QJsonArray files, sources;
    for ( const QString &name : { QStringLiteral( "main.mjs" ), QStringLiteral( "protocol.mjs" ), QStringLiteral( "controlled-session.mjs" ) } )
    {
      const auto data = name == QLatin1String( "main.mjs" ) ? fixture : QByteArray( "// fixture\n" );
      const QString relative = QStringLiteral( "libexec/sicnu-geo-rs/pi-product/" ) + name;
      write( QDir( prefix ).filePath( relative ), data );
      files.append( QJsonObject{ { QStringLiteral( "path" ), relative }, { QStringLiteral( "size" ), static_cast<double>( data.size() ) }, { QStringLiteral( "sha256" ), hash( data ) } } );
      sources.append( QJsonObject{ { QStringLiteral( "path" ), QStringLiteral( "pi/product-runtime/" ) + name }, { QStringLiteral( "sha256" ), hash( data ) } } );
    }
    const auto recipeHash = hash( QJsonDocument( sdk.value( QStringLiteral( "recipes" ) ).toObject() ).toJson( QJsonDocument::Compact ) );
    const QJsonObject product{ { QStringLiteral( "schema" ), QStringLiteral( "sicnu.pi-product/1" ) }, { QStringLiteral( "protocol_version" ), 1 },
      { QStringLiteral( "entry" ), QStringLiteral( "libexec/sicnu-geo-rs/pi-product/main.mjs" ) },
      { QStringLiteral( "requires_sdk" ), QJsonObject{ { QStringLiteral( "target" ), sdk.value( QStringLiteral( "target" ) ) },
        { QStringLiteral( "pi_version" ), QStringLiteral( "1.5.2" ) }, { QStringLiteral( "node_version" ), QStringLiteral( "24.22.3" ) },
        { QStringLiteral( "recipe_digest" ), recipeHash }, { QStringLiteral( "receipt_sha256" ), hash( sdkBytes ) } } },
      { QStringLiteral( "files" ), files }, { QStringLiteral( "sources" ), sources } };
    write( QDir( prefix ).filePath( QStringLiteral( "share/sicnu-geo-rs/pi-product.json" ) ), QJsonDocument( product ).toJson( QJsonDocument::Compact ) );
  }

  PiProcess::Config config( const QString &mode = QStringLiteral( "valid" ) ) const
  {
    PiProcess::Config value;
    value.prefixPath = prefix; value.sessionId = QStringLiteral( "session-fixture" ); value.generation = QStringLiteral( "opaque-generation" );
    value.apiKey = QByteArray( "secret-fixture-DoNotLog" );
    value.profile = { { QStringLiteral( "baseUrl" ), QStringLiteral( "http://127.0.0.1:9/v1" ) }, { QStringLiteral( "model" ), QStringLiteral( "fixture" ) },
      { QStringLiteral( "contextWindow" ), 32768 }, { QStringLiteral( "maxTokens" ), 4096 } };
    value.systemPrompt = QString::fromUtf8( QJsonDocument( QJsonObject{ { QStringLiteral( "mode" ), mode }, { QStringLiteral( "marker" ), marker },
      { QStringLiteral( "forbiddenHome" ), QDir::homePath() } } ).toJson( QJsonDocument::Compact ) );
    value.tools = { QJsonObject{ { QStringLiteral( "name" ), QStringLiteral( "host_tool" ) }, { QStringLiteral( "canonicalId" ), QStringLiteral( "python:run_script" ) },
      { QStringLiteral( "description" ), QStringLiteral( "Fixture only" ) }, { QStringLiteral( "parameters" ), QJsonObject{ { QStringLiteral( "type" ), QStringLiteral( "object" ) } } } } };
    value.readyTimeoutMs = 10000;
    return value;
  }
};

bool nodeAvailable() { return !qEnvironmentVariableIsEmpty( "SICNU_TEST_PI_NODE" ); }
} // namespace

TEST_CASE( "Pi process rejects a relative prefix without starting a child", "[agent][pi][process]" )
{
  application();
  PiProcess process;
  int failures = 0;
  QObject::connect( &process, &PiProcess::failed, [&]( const QString &, const QString &message ) { ++failures; CHECK_FALSE( message.contains( "secret" ) ); } );
  PiProcess::Config config;
  config.prefixPath = QStringLiteral( "relative" ); config.apiKey = "secret";
  process.start( config );
  CHECK( failures == 1 );
  CHECK( process.state() == PiProcess::State::Closed );
}

TEST_CASE( "Pi process verifies before credentials and rejects modified or extra resources", "[agent][pi][process]" )
{
  application();
  if ( !nodeAvailable() ) SKIP( "Requires pinned SICNU_TEST_PI_NODE from an explicitly prepared SDK stage" );
  Prefix files;
  SECTION( "Modified entry" ) { write( QDir( files.prefix ).filePath( QStringLiteral( "libexec/sicnu-geo-rs/pi-product/main.mjs" ) ), "changed" ); }
  SECTION( "Extra file" ) { write( QDir( files.prefix ).filePath( QStringLiteral( "libexec/sicnu-geo-rs/pi-product/extra.mjs" ) ), "extra" ); }
  SECTION( "Extra empty directory" ) { REQUIRE( QDir().mkpath( QDir( files.prefix ).filePath( QStringLiteral( "libexec/sicnu-geo-rs/pi-product/unrecorded" ) ) ) ); }
  SECTION( "Missing Node" ) { REQUIRE( QFile::remove( QDir( files.prefix ).filePath( QStringLiteral( "libexec/sicnu-geo-rs/node/node.exe" ) ) ) ); }
  SECTION( "Wrong platform" ) { files.sdk.insert( QStringLiteral( "target" ), QStringLiteral( "aarch64-linux" ) ); files.receipts(); }
  SECTION( "Changed recipe identity" ) { files.sdk.insert( QStringLiteral( "recipes" ), QJsonObject{} ); files.receipts(); }
  PiProcess process;
  int failures = 0;
  QObject::connect( &process, &PiProcess::failed, [&]( const QString &, const QString & ) { ++failures; } );
  process.start( files.config() );
  REQUIRE( until( [&] { return process.state() == PiProcess::State::Closed; } ) );
  CHECK( failures == 1 );
  CHECK_FALSE( QFileInfo::exists( files.marker ) );
}

TEST_CASE( "Pi process owned Node round trip validates tool association and redacts stderr", "[agent][pi][process]" )
{
  application();
  if ( !nodeAvailable() ) SKIP( "Requires pinned SICNU_TEST_PI_NODE" );
  Prefix files;
  PiProcess process;
  int failures = 0, settled = 0;
  QString output;
  QObject::connect( &process, &PiProcess::failed, [&]( const QString &, const QString & ) { ++failures; } );
  QObject::connect( &process, &PiProcess::eventReceived, [&]( const QJsonObject &event ) {
    const QString type = event.value( QStringLiteral( "type" ) ).toString();
    if ( type == QLatin1String( "tool.request" ) )
    {
      CHECK_FALSE( process.completeTool( QStringLiteral( "stale" ), QStringLiteral( "call-1" ), {} ) );
      CHECK( process.completeTool( QStringLiteral( "run-1" ), QStringLiteral( "call-1" ), { { QStringLiteral( "assetId" ), QStringLiteral( "fixture-asset" ) } } ) );
      CHECK_FALSE( process.completeTool( QStringLiteral( "run-1" ), QStringLiteral( "call-1" ), {} ) );
    }
    if ( type == QLatin1String( "settled" ) ) ++settled;
    if ( type == QLatin1String( "content" ) ) output += event.value( QStringLiteral( "text" ) ).toString();
  } );
  process.start( files.config( QStringLiteral( "tool" ) ) );
  REQUIRE( until( [&] { return process.state() == PiProcess::State::Ready || failures; } ) );
  REQUIRE( failures == 0 );
  CHECK_FALSE( process.sendPrompt( QStringLiteral( "too-big" ), QString( 65537, QLatin1Char( 'x' ) ) ) );
  REQUIRE( process.sendPrompt( QStringLiteral( "run-1" ), QStringLiteral( "fixture" ) ) );
  REQUIRE( until( [&] { return settled == 1 || failures; } ) );
  CHECK( output == QStringLiteral( "asset:fixture-asset" ) );
  process.close();
  REQUIRE( until( [&] { return process.state() == PiProcess::State::Closed; } ) );
  CHECK( failures == 0 );

  PiProcess noisy;
  noisy.start( files.config( QStringLiteral( "stderr" ) ) );
  REQUIRE( until( [&] { return noisy.state() == PiProcess::State::Ready || noisy.state() == PiProcess::State::Closed; } ) );
  REQUIRE( noisy.state() == PiProcess::State::Ready );
  REQUIRE( until( [&] { return noisy.stderrTail().contains( QStringLiteral( "exceeded limit" ) ); } ) );
  CHECK_FALSE( noisy.stderrTail().contains( QStringLiteral( "secret-fixture-DoNotLog" ) ) );
  CHECK( noisy.stderrTail().contains( QStringLiteral( "[redacted]" ) ) );
  CHECK( noisy.stderrTail().toUtf8().size() <= 256 * 1024 );
  noisy.close();
  REQUIRE( until( [&] { return noisy.state() == PiProcess::State::Closed; } ) );
}

TEST_CASE( "Pi process refuses invalid identity frames and unexpected EOF without replay", "[agent][pi][process]" )
{
  application();
  if ( !nodeAvailable() ) SKIP( "Requires pinned SICNU_TEST_PI_NODE" );
  Prefix files;
  for ( const QString &mode : { QStringLiteral( "wrong-session" ), QStringLiteral( "wrong-generation" ), QStringLiteral( "wrong-seq" ),
                                QStringLiteral( "bad-ready" ), QStringLiteral( "malformed" ), QStringLiteral( "huge" ), QStringLiteral( "eof" ), QStringLiteral( "invalid-utf8" ) } )
  {
    INFO( mode.toStdString() );
    PiProcess process;
    int failures = 0;
    QObject::connect( &process, &PiProcess::failed, [&]( const QString &, const QString & ) { ++failures; } );
    QObject::connect( &process, &PiProcess::ready, [&]( const QJsonObject & ) { if ( mode == QLatin1String( "invalid-utf8" ) ) process.sendPrompt( QStringLiteral( "run" ), QStringLiteral( "fixture" ) ); } );
    process.start( files.config( mode ) );
    REQUIRE( until( [&] { return process.state() == PiProcess::State::Closed; } ) );
    CHECK( failures == 1 );
  }
}

TEST_CASE( "Pi process rejects foreign runs and unknown cancellation calls", "[agent][pi][process]" )
{
  application();
  if ( !nodeAvailable() ) SKIP( "Requires pinned SICNU_TEST_PI_NODE" );
  Prefix files;
  for ( const QString &mode : { QStringLiteral( "wrong-run" ), QStringLiteral( "wrong-call" ) } )
  {
    INFO( mode.toStdString() );
    PiProcess process;
    int failures = 0;
    QObject::connect( &process, &PiProcess::failed, [&]( const QString &, const QString & ) { ++failures; } );
    QObject::connect( &process, &PiProcess::ready, [&]( const QJsonObject & ) { CHECK( process.sendPrompt( QStringLiteral( "run" ), QStringLiteral( "fixture" ) ) ); } );
    QObject::connect( &process, &PiProcess::eventReceived, [&]( const QJsonObject &event ) {
      if ( event.value( QStringLiteral( "type" ) ) == QLatin1String( "tool.request" ) ) process.cancel();
    } );
    process.start( files.config( mode ) );
    REQUIRE( until( [&] { return process.state() == PiProcess::State::Closed; } ) );
    CHECK( failures == 1 );
  }
}

TEST_CASE( "Pi process deadlines and destruction bound session cleanup", "[agent][pi][process]" )
{
  application();
  if ( !nodeAvailable() ) SKIP( "Requires pinned SICNU_TEST_PI_NODE" );
  Prefix files;
  for ( const QString &mode : { QStringLiteral( "never-ready" ), QStringLiteral( "ignore-close" ) } )
  {
    INFO( mode.toStdString() );
    PiProcess process;
    int failures = 0;
    auto config = files.config( mode );
    config.readyTimeoutMs = mode == QLatin1String( "never-ready" ) ? 100 : 10000;
    config.closeTimeoutMs = 100;
    QObject::connect( &process, &PiProcess::failed, [&]( const QString &, const QString & ) { ++failures; } );
    QObject::connect( &process, &PiProcess::ready, [&]( const QJsonObject & ) { process.close(); } );
    process.start( config );
    REQUIRE( until( [&] { return process.state() == PiProcess::State::Closed; }, 15000 ) );
    CHECK( failures == 1 );
  }
  REQUIRE( QFile::remove( files.marker ) );
  QElapsedTimer destruction;
  destruction.start();
  {
    PiProcess preparing;
    preparing.start( files.config() );
    REQUIRE( preparing.state() == PiProcess::State::Verifying );
  }
  CHECK( destruction.elapsed() < 1500 );
  QCoreApplication::processEvents();
  CHECK_FALSE( QFileInfo::exists( files.marker ) );
}

TEST_CASE( "Pi process cancellation waits for the host result and preparation stays responsive", "[agent][pi][process]" )
{
  application();
  if ( !nodeAvailable() ) SKIP( "Requires pinned SICNU_TEST_PI_NODE" );
  Prefix files;
  PiProcess process;
  bool requested = false, cancelled = false, settled = false;
  int failures = 0;
  QObject::connect( &process, &PiProcess::failed, [&]( const QString &, const QString & ) { ++failures; } );
  QObject::connect( &process, &PiProcess::eventReceived, [&]( const QJsonObject &event ) {
    const QString type = event.value( QStringLiteral( "type" ) ).toString();
    if ( type == QLatin1String( "tool.request" ) ) { requested = true; process.cancel(); }
    if ( type == QLatin1String( "tool.cancel" ) )
    {
      cancelled = true;
      CHECK_FALSE( settled );
      CHECK( process.completeTool( QStringLiteral( "run" ), QStringLiteral( "call-1" ), { { QStringLiteral( "status" ), QStringLiteral( "canceled" ) } } ) );
    }
    if ( type == QLatin1String( "settled" ) ) { settled = true; CHECK( event.value( QStringLiteral( "aborted" ) ).toBool() ); }
  } );
  process.start( files.config( QStringLiteral( "cancel-tool" ) ) );
  REQUIRE( until( [&] { return process.state() == PiProcess::State::Ready || failures; } ) );
  REQUIRE( failures == 0 );
  REQUIRE( process.sendPrompt( QStringLiteral( "run" ), QStringLiteral( "fixture" ) ) );
  REQUIRE( until( [&] { return settled || failures; } ) );
  CHECK( requested ); CHECK( cancelled ); CHECK( failures == 0 );
  process.close();
  REQUIRE( until( [&] { return process.state() == PiProcess::State::Closed; } ) );

  PiProcess preparing;
  bool ticked = false;
  preparing.start( files.config() );
  QTimer::singleShot( 0, &preparing, [&] { ticked = true; preparing.close(); } );
  REQUIRE( until( [&] { return preparing.state() == PiProcess::State::Closed; } ) );
  CHECK( ticked );
}

TEST_CASE( "Pi process verifies and launches the complete installed SDK without a model request", "[agent][pi][process][installed]" )
{
  application();
  const QString prefix = qEnvironmentVariable( "SICNU_TEST_PI_PRODUCT_PREFIX" );
  if ( prefix.isEmpty() ) SKIP( "Requires SICNU_TEST_PI_PRODUCT_PREFIX containing the complete prepared SDK and product runtime" );
  REQUIRE( QDir::isAbsolutePath( prefix ) );
  const QByteArray receipt = bytes( QDir( prefix ).filePath( QStringLiteral( "share/sicnu-geo-rs/pi-runtime.json" ) ) );
  const auto sdk = QJsonDocument::fromJson( receipt ).object();
  REQUIRE( sdk.value( QStringLiteral( "node_version" ) ).toString() == QStringLiteral( "24.22.3" ) );
  REQUIRE( sdk.value( QStringLiteral( "pi_version" ) ).toString() == QStringLiteral( "1.5.2" ) );
  REQUIRE( sdk.value( QStringLiteral( "files" ) ).toArray().size() > 10000 );

  PiProcess::Config config;
  config.prefixPath = prefix;
  config.sessionId = QStringLiteral( "installed-sdk-startup-oracle" );
  config.generation = QStringLiteral( "installed-sdk-generation" );
  config.toolRevision = QStringLiteral( "installed-empty-catalog-v1" );
  config.apiKey = QByteArray( "offline-fixture-key-not-a-real-credential" );
  config.profile = { { QStringLiteral( "baseUrl" ), QStringLiteral( "https://model.invalid/v1" ) },
    { QStringLiteral( "model" ), QStringLiteral( "offline-startup-fixture" ) },
    { QStringLiteral( "contextWindow" ), 32768 }, { QStringLiteral( "maxTokens" ), 4096 } };
  config.systemPrompt = QStringLiteral( "Offline startup oracle. No prompt or model request is submitted." );
  // No receipt or payload is rewritten: this exercises production verification,
  // Qt-owned launch/containment, and the actual SDK's private initialization.
  PiProcess process;
  QJsonObject handshake;
  QString failureCode;
  int failures = 0, toolRequests = 0;
  bool closedEvent = false;
  QObject::connect( &process, &PiProcess::ready, [&]( const QJsonObject &event ) { handshake = event; } );
  QObject::connect( &process, &PiProcess::failed, [&]( const QString &code, const QString & ) { ++failures; failureCode = code; } );
  QObject::connect( &process, &PiProcess::eventReceived, [&]( const QJsonObject &event ) {
    const QString type = event.value( QStringLiteral( "type" ) ).toString();
    if ( type == QLatin1String( "tool.request" ) ) ++toolRequests;
    if ( type == QLatin1String( "closed" ) ) closedEvent = true;
  } );
  process.start( config );
  const bool reachedHandshake = until( [&] { return !handshake.isEmpty() || process.state() == PiProcess::State::Closed; },
    config.verifyTimeoutMs + config.readyTimeoutMs + 1000 );
  INFO( failureCode.toStdString() );
  REQUIRE( reachedHandshake );
  REQUIRE( failures == 0 );
  REQUIRE( process.state() == PiProcess::State::Ready );
  CHECK( handshake.value( QStringLiteral( "nodeVersion" ) ).toString() == QStringLiteral( "24.22.3" ) );
  CHECK( handshake.value( QStringLiteral( "piVersion" ) ).toString() == QStringLiteral( "1.5.2" ) );
  CHECK( handshake.value( QStringLiteral( "sdkReceiptSha256" ) ).toString() == hash( receipt ) );
  CHECK( handshake.value( QStringLiteral( "toolRevision" ) ).toString() == config.toolRevision );
  CHECK( toolRequests == 0 );
  process.close();
  REQUIRE( until( [&] { return process.state() == PiProcess::State::Closed; }, 10000 ) );
  CHECK( failures == 0 );
  CHECK( closedEvent );
  CHECK( toolRequests == 0 );
}
