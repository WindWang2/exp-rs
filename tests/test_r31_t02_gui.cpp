// tests/test_r31_t02_gui.cpp
// R31-T02 GUI E2E integration tests aligned with #2068/#2069/#2070:
//   - Stages a Linux host-Node installation prefix verified by PiProcess::verify()
//     speaking the `sicnu.pi/1` NDJSON protocol over OS pipes.
//   - Drives AgentCopilotDockWidget (with backend=pi) across both en and zh_CN
//     real compiled .qm locales:
//       prompt -> tool.request (rs:spectral_index) -> AgentToolHost approval gate
//       -> confirm button click -> TaskCenter/OutputCommitter/DataManager commit
//       -> GDAL 4x3 NDVI pixel verification (0.5) -> agentShowResult action
//       -> ConversationMetrics trace observation (no secrets)
//       -> ConversationCheckpointStore save -> fresh dock restore with 0 replays.
//   - Verifies AS-015 endpoint refusal in AgentCopilotDockWidget before spawn.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "agent/agent_copilot_dock_widget.h"
#include "agent/llm_config_manager.h"
#include "agent/runtime/pi_process.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "app/application_language.h"
#include "data/data_manager.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "runtime/observability/trace.h"
#include "support/application_translations.h"
#include "support/qsettings_redirect.h"
#include "support/qt_lifecycle.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

using namespace sicnu::agent;
using namespace sicnu::agent::runtime;
using namespace sicnu::data;
using namespace sicnu::processing;
namespace trace = sicnu::runtime::observability::trace;

namespace
{

exprs_test::QSettingsUserRootRedirect s_settingsRedirect;

void ensureQtApp()
{
  qputenv( "SICNU_LLM_KEYCHAIN", "0" );
  QSettings::setDefaultFormat( QSettings::IniFormat );
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "r31-t02-gui" ) );
  if ( !QApplication::instance() )
  {
    QCoreApplication::setAttribute( Qt::AA_DontUseNativeDialogs );
    static int argc = 1;
    static char appName[] = "test_r31_t02_gui";
    static char *argv[] = { appName, nullptr };
    new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
  }
}

bool until( const std::function<bool()> &predicate, int timeoutMs = 60000 )
{
  QElapsedTimer timer;
  timer.start();
  while ( !predicate() && timer.elapsed() < timeoutMs )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    QThread::msleep( 5 );
  }
  return predicate();
}

struct ScopedEnv
{
  const char *key;
  bool hadPrev;
  QByteArray prev;

  ScopedEnv( const char *k, const QByteArray &val )
    : key( k ), hadPrev( qEnvironmentVariableIsSet( k ) ), prev( qgetenv( k ) )
  {
    qputenv( key, val );
  }
  ~ScopedEnv()
  {
    if ( hadPrev )
      qputenv( key, prev );
    else
      qunsetenv( key );
  }
};

struct GdalDatasetCloser
{
  void operator()( GDALDataset *ds ) const
  {
    if ( ds )
      GDALClose( static_cast<GDALDatasetH>( ds ) );
  }
};

using GdalDatasetPtr = std::unique_ptr<GDALDataset, GdalDatasetCloser>;

bool writeRedNirGeoTiff( const QString &path, int width, int height, float redVal, float nirVal )
{
  GDALAllRegister();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GdalDatasetPtr ds( driver->Create( path.toUtf8().constData(), width, height, 2, GDT_Float32, nullptr ) );
  if ( !ds )
    return false;
  std::array<double, 6> gt = { 300000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 };
  ds->SetGeoTransform( gt.data() );
  OGRSpatialReference srs;
  srs.importFromEPSG( 32648 );
  char *wkt = nullptr;
  srs.exportToWkt( &wkt );
  ds->SetProjection( wkt );
  CPLFree( wkt );

  std::vector<float> red( static_cast<size_t>( width ) * height, redVal );
  std::vector<float> nir( static_cast<size_t>( width ) * height, nirVal );
  const CPLErr r1 = ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, width, height, red.data(), width, height, GDT_Float32, 0, 0 );
  const CPLErr r2 = ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, 0, width, height, nir.data(), width, height, GDT_Float32, 0, 0 );
  return r1 == CE_None && r2 == CE_None;
}

QString sha256Hex( const QByteArray &data )
{
  return QString::fromLatin1( QCryptographicHash::hash( data, QCryptographicHash::Sha256 ).toHex() );
}

void writeFileBytes( const QString &path, const QByteArray &data, bool executable = false )
{
  REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
  QFile f( path );
  REQUIRE( f.open( QIODevice::WriteOnly ) );
  REQUIRE( f.write( data ) == data.size() );
  f.close();
  if ( executable )
    QFile::setPermissions( path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner );
}

QByteArray readFileBytes( const QString &path )
{
  QFile f( path );
  REQUIRE( f.open( QIODevice::ReadOnly ) );
  return f.readAll();
}

QString findHostNodeExecutable()
{
#ifdef SICNU_TEST_HOST_NODE_EXECUTABLE
  const QString compiled = QStringLiteral( SICNU_TEST_HOST_NODE_EXECUTABLE );
  if ( !compiled.isEmpty() && QFileInfo::exists( compiled ) )
    return compiled;
#endif
  return QStandardPaths::findExecutable( QStringLiteral( "node" ) );
}

struct StagedPiFixturePrefix
{
  QTemporaryDir root;
  QString prefix;

  void stageWithMainJs( const QString &nodeExecutable, const QByteArray &mainMjs )
  {
    REQUIRE( root.isValid() );
    prefix = root.filePath( QStringLiteral( "pi-product-fixture" ) );
    const QString sourceDir = QStringLiteral( SICNU_SOURCE_DIR );

    static const QStringList kRecipeKeys{
      QStringLiteral( "vendor/node/runtime.lock.json" ),
      QStringLiteral( "vendor/pi/package-lock.json" ),
      QStringLiteral( "vendor/pi/package.json" ),
      QStringLiteral( "vendor/pi/patches/0001-model-runtime-auth-context.dist.patch" ),
      QStringLiteral( "vendor/pi/patches/0001-model-runtime-auth-context.source.patch" ),
      QStringLiteral( "vendor/pi/patches/manifest.json" ),
      QStringLiteral( "vendor/pi/runtime.lock.json" ),
    };

    QJsonArray sdkFiles;
    auto addSdkFile = [&]( const QString &relPath, const QByteArray &bytes, bool executable = false ) {
      writeFileBytes( QDir( prefix ).filePath( relPath ), bytes, executable );
      sdkFiles.append( QJsonObject{
        { QStringLiteral( "path" ), relPath },
        { QStringLiteral( "size" ), static_cast<double>( bytes.size() ) },
        { QStringLiteral( "sha256" ), sha256Hex( bytes ) },
      } );
    };

    addSdkFile( QStringLiteral( "libexec/sicnu-geo-rs/node/node.exe" ), readFileBytes( nodeExecutable ), true );

    QJsonObject recipes;
    for ( const QString &key : kRecipeKeys )
    {
      const QByteArray data = readFileBytes( QDir( sourceDir ).filePath( key ) );
      recipes.insert( key, sha256Hex( data ) );
      addSdkFile( QStringLiteral( "share/sicnu-geo-rs/pi/" ) + key, data );
    }
    REQUIRE( QDir().mkpath( QDir( prefix ).filePath( QStringLiteral( "libexec/sicnu-geo-rs/pi-agent" ) ) ) );

    const QString recipeDigest = sha256Hex( QJsonDocument( recipes ).toJson( QJsonDocument::Compact ) );
    REQUIRE( recipeDigest == QStringLiteral( SICNU_PI_RECIPE_DIGEST ) );

    const QJsonObject sdkReceipt{
      { QStringLiteral( "schema" ), QStringLiteral( "sicnu.pi-runtime/1" ) },
      { QStringLiteral( "target" ), QStringLiteral( "x86_64-pc-windows-msvc" ) },
      { QStringLiteral( "pi_version" ), QStringLiteral( "1.5.2" ) },
      { QStringLiteral( "node_version" ), QStringLiteral( "24.22.3" ) },
      { QStringLiteral( "recipes" ), recipes },
      { QStringLiteral( "files" ), sdkFiles },
    };
    const QByteArray sdkBytes = QJsonDocument( sdkReceipt ).toJson( QJsonDocument::Compact );
    writeFileBytes( QDir( prefix ).filePath( QStringLiteral( "share/sicnu-geo-rs/pi-runtime.json" ) ), sdkBytes );

    QJsonArray productFiles;
    QJsonArray sources;
    for ( const QString &name : { QStringLiteral( "checkpoint.mjs" ),
                                  QStringLiteral( "controlled-session.mjs" ),
                                  QStringLiteral( "main.mjs" ),
                                  QStringLiteral( "protocol.mjs" ) } )
    {
      QByteArray data;
      if ( name == QLatin1String( "main.mjs" ) )
        data = mainMjs;
      else if ( name == QLatin1String( "protocol.mjs" ) || name == QLatin1String( "checkpoint.mjs" ) )
        data = readFileBytes( QDir( sourceDir ).filePath( QStringLiteral( "pi/product-runtime/" ) + name ) );
      else
        data = QByteArray( "// Controlled session stubbed in Linux host-Node harness without Pi SDK.\n" );
      const QString relPath = QStringLiteral( "libexec/sicnu-geo-rs/pi-product/" ) + name;
      writeFileBytes( QDir( prefix ).filePath( relPath ), data );
      productFiles.append( QJsonObject{
        { QStringLiteral( "path" ), relPath },
        { QStringLiteral( "size" ), static_cast<double>( data.size() ) },
        { QStringLiteral( "sha256" ), sha256Hex( data ) },
      } );
      sources.append( QJsonObject{
        { QStringLiteral( "path" ), QStringLiteral( "pi/product-runtime/" ) + name },
        { QStringLiteral( "sha256" ), sha256Hex( data ) },
      } );
    }

    const QJsonObject productReceipt{
      { QStringLiteral( "schema" ), QStringLiteral( "sicnu.pi-product/1" ) },
      { QStringLiteral( "protocol_version" ), 1 },
      { QStringLiteral( "entry" ), QStringLiteral( "libexec/sicnu-geo-rs/pi-product/main.mjs" ) },
      { QStringLiteral( "requires_sdk" ), QJsonObject{
        { QStringLiteral( "target" ), QStringLiteral( "x86_64-pc-windows-msvc" ) },
        { QStringLiteral( "pi_version" ), QStringLiteral( "1.5.2" ) },
        { QStringLiteral( "node_version" ), QStringLiteral( "24.22.3" ) },
        { QStringLiteral( "recipe_digest" ), recipeDigest },
        { QStringLiteral( "receipt_sha256" ), sha256Hex( sdkBytes ) },
      } },
      { QStringLiteral( "files" ), productFiles },
      { QStringLiteral( "sources" ), sources },
    };
    writeFileBytes( QDir( prefix ).filePath( QStringLiteral( "share/sicnu-geo-rs/pi-product.json" ) ),
                    QJsonDocument( productReceipt ).toJson( QJsonDocument::Compact ) );
  }
};

QByteArray buildControlledPiMainJs()
{
  // Drives the real product protocol.mjs (PrivateSession, JsonLineDecoder, redaction, budgets)
  // and real checkpoint.mjs (captureCheckpoint, parseCheckpoint, restoreCheckpoint) over sicnu.pi/1.
  return QByteArray( R"JS(
import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { JsonLineDecoder, PrivateSession, ProtocolError, LIMITS } from './protocol.mjs';
import { restoreCheckpoint } from './checkpoint.mjs';

class FakeSessionManager {
  #header;
  #entries;
  #leafId;
  #seq;
  constructor(header, entries, leafId) {
    this.#header = header;
    this.#entries = entries;
    this.#leafId = leafId;
    this.#seq = entries.length;
  }
  static inMemory(cwd, _unused, items = []) {
    const header = items.length ? { ...items[0], cwd } : {
      type: 'session', version: 3, id: 'sdk-fixture-1', timestamp: '2026-11-16T00:00:00.000Z', cwd,
    };
    const entries = items.length > 1 ? structuredClone(items.slice(1)) : [];
    const leafId = entries.length ? entries[entries.length - 1].id : null;
    return new FakeSessionManager(header, entries, leafId);
  }
  resetLeaf() { this.#leafId = null; }
  branch(id) { this.#leafId = id; }
  getHeader() { return this.#header; }
  getEntries() { return this.#entries; }
  getLeafId() { return this.#leafId; }
  appendMessage(message) {
    const id = 'm-' + (++this.#seq);
    this.#entries.push({ type: 'message', id, parentId: this.#leafId, timestamp: '2026-11-16T00:00:00.000Z', message });
    this.#leafId = id;
  }
  buildSessionContext() {
    const byId = new Map(this.#entries.map((e) => [e.id, e]));
    const active = [];
    for (let id = this.#leafId; id !== null; id = byId.get(id)?.parentId ?? null) active.push(byId.get(id));
    active.reverse();
    return { messages: active.filter((e) => e.type === 'message').map((e) => e.message) };
  }
}

async function createMockControlled({ cwd, profile, tools, checkpoint, checkpointBindings, onToolRequest }) {
  if (process.env.OPENAI_API_KEY || process.argv.includes(profile.apiKey)) {
    throw new Error('SECRET_LEAK');
  }
  let manager;
  let restored = false;
  if (checkpoint) {
    manager = restoreCheckpoint(FakeSessionManager, checkpoint, {
      conversationId: checkpointBindings.conversationId,
      projectId: checkpointBindings.projectId,
      systemPromptSha256: checkpointBindings.systemPromptSha256,
      toolRevision: checkpointBindings.toolRevision,
      sdkReceiptSha256: checkpointBindings.sdkReceiptSha256,
      allowedToolNames: checkpointBindings.allowedToolNames,
      currentOwnedCwd: cwd,
    });
    restored = true;
  } else {
    manager = FakeSessionManager.inMemory(cwd);
  }

  const sessionSubscribers = new Set();
  const agentSubscribers = new Set();
  const emitSession = (ev) => { for (const fn of sessionSubscribers) fn(ev); };
  const session = {
    isIdle: true,
    sessionManager: manager,
    agent: {
      async streamFunction() {
        return {
          push() {},
          async *[Symbol.asyncIterator]() {},
          async result() { return { usage: { input: 24, output: 12, cacheRead: 0, cacheWrite: 0, totalTokens: 36 } }; },
        };
      },
      subscribe(fn) { agentSubscribers.add(fn); return () => agentSubscribers.delete(fn); },
    },
    subscribe(fn) { sessionSubscribers.add(fn); return () => sessionSubscribers.delete(fn); },
    async abort() {},
    dispose() {},
  };

  return {
    session,
    async prompt(rawText) {
      session.isIdle = false;
      const turn = JSON.parse(rawText);
      const userText = turn.userText || rawText;
      const marker = userText.indexOf('PARAMS:');
      const params = marker >= 0 ? JSON.parse(userText.slice(marker + 7)) : {};
      if (params.expectedLang && turn.replyLanguage !== params.expectedLang) {
        throw new Error('LANGUAGE_MISMATCH');
      }

      const stream = await session.agent.streamFunction({}, {}, {});
      for await (const _chunk of stream) {}
      await stream.result();

      const u = { input: 24, output: 12, cacheRead: 0, cacheWrite: 0, totalTokens: 36, cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0, total: 0 } };
      manager.appendMessage({ role: 'user', content: rawText, timestamp: 1 });

      let answer;
      if (restored) {
        const priorEntries = manager.getEntries().length - 1;
        answer = 'RESTORED_FOLLOWUP:prior=' + priorEntries;
      } else {
        const spectral = tools.find((t) => t.canonicalId === 'rs:spectral_index');
        if (!spectral) throw new Error('MISSING_SPECTRAL_TOOL');
        for (const fn of agentSubscribers) await fn({ type: 'tool_execution_start' });
        const callArgs = { input: params.input, output: params.output, index: 'NDVI', red: 1, nir: 2 };
        manager.appendMessage({
          role: 'assistant',
          content: [{ type: 'toolCall', id: 'pi-call-ndvi-1', name: spectral.name, arguments: callArgs }],
          api: 'openai-completions', provider: 'exp-rs-product', model: 'fixture-pi-model', usage: u, stopReason: 'toolUse', timestamp: 2,
        });
        const result = await onToolRequest({
          toolCallId: 'pi-call-ndvi-1',
          canonicalId: 'rs:spectral_index',
          arguments: callArgs,
        });
        const ok = result && (result.status === 'success' || result.status === 'ok');
        manager.appendMessage({
          role: 'toolResult',
          toolCallId: 'pi-call-ndvi-1',
          toolName: spectral.name,
          content: [{ type: 'text', text: JSON.stringify(result || {}) }],
          isError: !ok,
          timestamp: 3,
        });
        answer = ok ? ('PI_DELIVERED:' + (result.assetId || 'ok')) : 'PI_TOOL_ERROR';
      }

      manager.appendMessage({
        role: 'assistant',
        content: [{ type: 'text', text: answer }],
        api: 'openai-completions', provider: 'exp-rs-product', model: 'fixture-pi-model', usage: u, stopReason: 'stop', timestamp: 4,
      });
      emitSession({ type: 'message_update', assistantMessageEvent: { type: 'text_delta', delta: answer } });
      session.isIdle = true;
      emitSession({ type: 'agent_settled', aborted: false });
    },
  };
}

async function run() {
  const sdkBytes = await readFile(new URL('../../../share/sicnu-geo-rs/pi-runtime.json', import.meta.url));
  const sdkReceiptSha256 = createHash('sha256').update(sdkBytes).digest('hex');
  const decoder = new JsonLineDecoder();
  let stopped = false;
  const protocol = new PrivateSession({
    metadata: { nodeVersion: '24.22.3', piVersion: '1.5.2', sdkReceiptSha256 },
    createSession: createMockControlled,
    emit(frame) {
      const line = `${JSON.stringify(frame)}\n`;
      if (Buffer.byteLength(line) - 1 > LIMITS.frameBytes) throw new ProtocolError('OUTPUT_BACKPRESSURE');
      process.stdout.write(line);
    },
  });
  const terminate = async (reason, code = 0) => {
    if (stopped) return;
    stopped = true;
    try { await protocol.disconnect(reason); } catch {}
    await new Promise((done) => process.stdout.write('', done));
    process.exit(code);
  };
  for await (const chunk of process.stdin) {
    for (const frame of decoder.push(chunk)) {
      await protocol.accept(frame);
      if (frame.type === 'shutdown') { await terminate('shutdown', 0); return; }
    }
  }
  await terminate('eof', 0);
}

run().catch(() => { process.exit(1); });
)JS" );
}

} // namespace

TEST_CASE( "R31-T02 GUI [Pi]: bilingual staged host-Node sicnu.pi/1 approval, NDVI delivery, metrics, and non-replay restore",
           "[r31_t02][gui][pi][bilingual]" )
{
  ensureQtApp();
  const QString nodeBin = findHostNodeExecutable();
  if ( nodeBin.isEmpty() )
    SKIP( "No host Node binary found to run staged Linux OS-pipe Pi product fixture." );

  AtomicAlgorithmRegistry::instance().initialize();
  sicnu::agent::tool_catalog::AgentToolCatalog::instance().initializeDefaults();

  // Stage host Node + real product protocol.mjs / checkpoint.mjs once per test run.
  StagedPiFixturePrefix stagedPi;
  stagedPi.stageWithMainJs( nodeBin, buildControlledPiMainJs() );

  for ( const bool chinese : { false, true } )
  {
  CAPTURE( chinese );

  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  ScopedEnv envWs( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );
  ScopedEnv envWorker( "SICNU_WORKER_EXECUTION", "off" );
  ScopedEnv envLang( "SICNU_LANG", "" );

  const QString projectPath = workspace.filePath( chinese ? QStringLiteral( "proj_zh.qgz" ) : QStringLiteral( "proj_en.qgz" ) );
  writeFileBytes( projectPath, "qgz-fixture" );
  const QString previousProject = QgsProject::instance()->fileName();
  QgsProject::instance()->setFileName( projectPath );
  const auto restoreProject = qScopeGuard( [&] { QgsProject::instance()->setFileName( previousProject ); } );

  QSettings uiSettings;
  const QString langKey = QStringLiteral( "preferences/uiLanguage" );
  const bool hadLang = uiSettings.contains( langKey );
  const QVariant prevLang = uiSettings.value( langKey );
  exprs_test::removeApplicationTranslators();
  uiSettings.setValue( langKey, chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) );
  const auto restoreLang = qScopeGuard( [&] {
    exprs_test::removeApplicationTranslators();
    if ( hadLang )
      uiSettings.setValue( langKey, prevLang );
    else
      uiSettings.remove( langKey );
  } );

  const QString appliedLang = SicnuUi::installUiTranslations(
    *qApp, { QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR ) } );
  REQUIRE( appliedLang == ( chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) ) );

  const QString inputTif = workspace.filePath( QStringLiteral( "pi_red_nir.tif" ) );
  const QString outputTif = workspace.filePath( QStringLiteral( "pi_ndvi_out.tif" ) );
  REQUIRE( writeRedNirGeoTiff( inputTif, 4, 3, 0.2f, 0.6f ) );
  const QString checkpointDir = workspace.filePath( QStringLiteral( "pi-checkpoints" ) );

  // Install ring trace sink to verify ConversationMetrics emission without leaking secrets.
  const std::shared_ptr<trace::ITraceSink> previousSink = trace::Trace::sink();
  const auto ringSink = std::make_shared<trace::RingTraceSink>( 64 );
  trace::Trace::install( ringSink );
  const auto restoreSink = qScopeGuard( [&] { trace::Trace::install( previousSink ); } );

  const QString prevBackend = QSettings().value( QStringLiteral( "agent/conversationBackend" ) ).toString();
  QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "pi" ) );
  const auto restoreBackend = qScopeGuard( [&] {
    QSettings().setValue( QStringLiteral( "agent/conversationBackend" ),
                          prevBackend.isEmpty() ? QStringLiteral( "native" ) : prevBackend );
  } );

  DataManager dataMgr;
  auto &taskCenter = sicnu::TaskCenter::instance();
  taskCenter.setCatalog( &dataMgr );
  const auto resetCatalog = qScopeGuard( [&] { taskCenter.setCatalog( nullptr ); } );

  QgsMapCanvas canvas;
  AgentCopilotDockWidget dock( nullptr, taskCenter, stagedPi.prefix, checkpointDir );
  dock.setContext( &dataMgr, &canvas );

  // Configure AS-015 safe loopback profile with synthetic API key.
  LlmProviderProfile profile;
  profile.id = QStringLiteral( "pi-r31-fixture" );
  profile.name = QStringLiteral( "Pi R31 Fixture" );
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:11434/v1" );
  profile.modelName = QStringLiteral( "fixture-pi-model" );
  profile.apiKey = QStringLiteral( "sk-pi-secret-not-for-logs" );
  LlmConfigManager::setActiveProfile( profile );

  long spectralTaskId = -1;
  bool taskCompleted = false;
  QObject observer;
  QObject::connect( &taskCenter, &sicnu::TaskCenter::taskAdded, &observer, [&]( const sicnu::AlgorithmTaskInfo &info ) {
    if ( info.algorithmId == QStringLiteral( "rs:spectral_index" ) )
      spectralTaskId = info.taskId;
  } );
  QObject::connect( &taskCenter, &sicnu::TaskCenter::taskUpdated, &observer, [&]( const sicnu::AlgorithmTaskInfo &info ) {
    if ( info.taskId == spectralTaskId && info.status == sicnu::TaskStatus::Completed )
      taskCompleted = true;
  } );

  const QJsonObject promptParams{
    { QStringLiteral( "input" ), inputTif },
    { QStringLiteral( "output" ), outputTif },
    { QStringLiteral( "expectedLang" ), appliedLang },
  };
  dock.sendPrompt( QStringLiteral( "Run rs:spectral_index NDVI workflow PARAMS:" )
                   + QString::fromUtf8( QJsonDocument( promptParams ).toJson( QJsonDocument::Compact ) ) );

  bool sawStart = dock.isRunActive();
  REQUIRE( until( [&] {
    sawStart |= dock.isRunActive();
    if ( dock.findChild<QPushButton *>( QStringLiteral( "agentApproveOperation" ) ) != nullptr )
      return true;
    if ( sawStart && !dock.isRunActive() )
      return true;
    return false;
  }, 60000 ) );
  INFO( "inspector=" << dock.runInspectorSummary().toStdString() );
  REQUIRE( dock.findChild<QPushButton *>( QStringLiteral( "agentApproveOperation" ) ) != nullptr );
  CHECK( spectralTaskId == -1 );
  CHECK_FALSE( QFileInfo::exists( outputTif ) );

  auto *approveBtn = dock.findChild<QPushButton *>( QStringLiteral( "agentApproveOperation" ) );
  REQUIRE( approveBtn != nullptr );
  approveBtn->click();

  REQUIRE( until( [&] {
    return taskCompleted && !dock.isRunActive();
  }, 60000 ) );
  CHECK( spectralTaskId > 0 );

  // Verify card JSON delivered committed path & click Show Result on Map.
  const auto *details = dock.findChild<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) );
  REQUIRE( details != nullptr );
  const QJsonObject resultJson = QJsonDocument::fromJson( details->text().toUtf8() ).object();
  const QString committedPath = resultJson.value( QStringLiteral( "output" ) ).toString();
  const QString committedAssetId = resultJson.value( QStringLiteral( "assetId" ) ).toString();
  REQUIRE_FALSE( committedAssetId.isEmpty() );
  REQUIRE( QFileInfo::exists( committedPath ) );

  auto *showBtn = dock.findChild<QPushButton *>( QStringLiteral( "agentShowResult" ) );
  REQUIRE( showBtn != nullptr );
  QString shownAsset;
  QObject::connect( &dock, &AgentCopilotDockWidget::showResultOnMapRequested,
                    &observer, [&]( const QString &id ) { shownAsset = id; } );
  showBtn->click();
  CHECK( shownAsset == committedAssetId );

  // Verify GDAL output pixels == 0.5f on all 4x3 cells and no secrets in UI labels.
  GDALAllRegister();
  GdalDatasetPtr outDs( static_cast<GDALDataset *>( GDALOpen( committedPath.toUtf8().constData(), GA_ReadOnly ) ) );
  REQUIRE( outDs != nullptr );
  std::vector<float> pixels( 12, 0.0f );
  REQUIRE( outDs->GetRasterBand( 1 )->RasterIO( GF_Read, 0, 0, 4, 3, pixels.data(), 4, 3, GDT_Float32, 0, 0 ) == CE_None );
  for ( float p : pixels )
    CHECK( p == Catch::Approx( 0.5f ).epsilon( 1e-4 ) );

  bool sawDeliveredBubble = false;
  for ( const QLabel *lbl : dock.findChildren<QLabel *>() )
  {
    CHECK_FALSE( lbl->text().contains( QStringLiteral( "sk-pi-secret-not-for-logs" ) ) );
    if ( lbl->text().contains( QStringLiteral( "PI_DELIVERED:" ) ) )
      sawDeliveredBubble = true;
  }
  CHECK( sawDeliveredBubble );

  // Verify ConversationMetrics emitted agent.run.metrics for Pi without leaking secrets or prompt.
  const auto traceEvents = ringSink->snapshot();
  bool foundPiMetrics = false;
  for ( const auto &ev : traceEvents )
  {
    if ( ev.event == "agent.run.metrics" )
    {
      foundPiMetrics = true;
      const QJsonObject detailObj = QJsonDocument::fromJson( QByteArray::fromStdString( ev.detail ) ).object();
      CHECK( detailObj.value( QStringLiteral( "backend" ) ).toString() == QStringLiteral( "pi" ) );
      CHECK( detailObj.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "ok" ) );
      CHECK( detailObj.value( QStringLiteral( "tokens" ) ).toObject().value( QStringLiteral( "totalTokens" ) ).toInt() == 36 );
      const QByteArray encoded = QByteArray::fromStdString( trace::encodeNdjson( ev ) );
      CHECK_FALSE( encoded.contains( "sk-pi-secret" ) );
      CHECK_FALSE( encoded.contains( "Run rs:spectral_index" ) );
    }
  }
  CHECK( foundPiMetrics );

  // Verify real checkpoint.mjs restoreCheckpoint round-trip + follow-up turn with zero task replays.
  const qsizetype tasksBeforeRestore = taskCenter.allTasks().size();
  AgentCopilotDockWidget restoredDock( nullptr, taskCenter, stagedPi.prefix, checkpointDir );
  restoredDock.setContext( &dataMgr, &canvas );
  auto *restoreBtn = restoredDock.findChild<QPushButton *>( QStringLiteral( "agentRestoreConversation" ) );
  REQUIRE( restoreBtn != nullptr );
  restoreBtn->click();
  REQUIRE( until( [&] { return !restoredDock.isRunActive(); }, 60000 ) );

  bool sawRestoredAnswer = false;
  for ( const QLabel *lbl : restoredDock.findChildren<QLabel *>() )
    if ( lbl->text().contains( QStringLiteral( "PI_DELIVERED:" ) ) )
      sawRestoredAnswer = true;
  CHECK( sawRestoredAnswer );
  CHECK( taskCenter.allTasks().size() == tasksBeforeRestore );

  // Send follow-up prompt after restore: proves PrivateSession initialized from m.checkpoint and retained 4 prior entries.
  restoredDock.sendPrompt( QStringLiteral( "Follow-up after restore" ) );
  REQUIRE( until( [&] { return !restoredDock.isRunActive(); }, 60000 ) );
  bool sawFollowUp = false;
  for ( const QLabel *lbl : restoredDock.findChildren<QLabel *>() )
    if ( lbl->text().contains( QStringLiteral( "RESTORED_FOLLOWUP:prior=4" ) ) )
      sawFollowUp = true;
  CHECK( sawFollowUp );
  CHECK( taskCenter.allTasks().size() == tasksBeforeRestore );
  }
}

TEST_CASE( "R31-T02 GUI [Pi]: bilingual endpoint refusal precedes spawn and permits retry",
           "[r31_t02][gui][pi][security]" )
{
  ensureQtApp();
  AtomicAlgorithmRegistry::instance().initialize();
  sicnu::agent::tool_catalog::AgentToolCatalog::instance().initializeDefaults();

  const QString prevBackend = QSettings().value( QStringLiteral( "agent/conversationBackend" ) ).toString();
  QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "pi" ) );
  const auto restoreBackend = qScopeGuard( [&] {
    QSettings().setValue( QStringLiteral( "agent/conversationBackend" ),
                          prevBackend.isEmpty() ? QStringLiteral( "native" ) : prevBackend );
  } );

  ScopedEnv envLang( "SICNU_LANG", "" );
  QSettings uiSettings;
  const QString langKey = QStringLiteral( "preferences/uiLanguage" );
  const bool hadLang = uiSettings.contains( langKey );
  const QVariant prevLang = uiSettings.value( langKey );
  const auto restoreLang = qScopeGuard( [&] {
    exprs_test::removeApplicationTranslators();
    if ( hadLang ) uiSettings.setValue( langKey, prevLang );
    else uiSettings.remove( langKey );
  } );

  for ( const bool chinese : { false, true } )
  {
    CAPTURE( chinese );
    exprs_test::removeApplicationTranslators();
    uiSettings.setValue( langKey, chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) );
    const QString appliedLang = SicnuUi::installUiTranslations(
      *qApp, { QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR ) } );
    REQUIRE( appliedLang == ( chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) ) );
    if ( chinese )
    {
      REQUIRE( QFileInfo::exists( QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR "/sicnu_zh_CN.qm" ) ) );
      REQUIRE( qApp->findChild<QTranslator *>( QStringLiteral( "sicnuAppTranslator" ) ) != nullptr );
      REQUIRE( qApp->findChild<QTranslator *>( QStringLiteral( "sicnuQtTranslator" ) ) != nullptr );
    }

    for ( const bool malformed : { false, true } )
    {
      CAPTURE( malformed );
      QTemporaryDir workspace;
      REQUIRE( workspace.isValid() );
      const QString prefix = workspace.filePath( QStringLiteral( "receipt-prefix" ) );
      writeFileBytes( QDir( prefix ).filePath( QStringLiteral( "share/sicnu-geo-rs/pi-runtime.json" ) ),
                      "{\"fixture\":\"receipt-only\"}" );
      const QString checkpointDir = workspace.filePath( QStringLiteral( "ckpt" ) );

      LlmProviderProfile badProfile;
      badProfile.id = QStringLiteral( "insecure-pi-profile" );
      badProfile.name = QStringLiteral( "Insecure Pi Profile" );
      badProfile.baseUrl = malformed ? QStringLiteral( "https://model.invalid/v1?token=not-allowed" )
                                     : QStringLiteral( "http://api.example.invalid/v1" );
      badProfile.modelName = QStringLiteral( "fixture-model" );
      badProfile.apiKey = QStringLiteral( "sk-must-not-transmit-over-http" );
      LlmConfigManager::setActiveProfile( badProfile );

      const QString expectedReason = malformed
        ? ( chinese ? QStringLiteral( "拒绝使用无效的模型端点 URL；请使用不含内嵌凭据、查询参数或片段的 HTTP(S) 基础 URL。" )
                    : QStringLiteral( "Refusing invalid model endpoint URL; expected an HTTP(S) base URL without embedded credentials, query or fragment." ) )
        : ( chinese ? QStringLiteral( "拒绝通过明文 HTTP 向非回环主机（api.example.invalid）发送 API 密钥；请使用 HTTPS 端点或本地运行时。" )
                    : QStringLiteral( "Refusing to send the API key over plaintext http to a non-loopback host (api.example.invalid); use an https endpoint or a local runtime." ) );
      const QString genericError = QCoreApplication::translate(
        "sicnu::agent::runtime::PiConversationRuntime", "The Pi conversation could not continue." );
      DataManager dataMgr;
      auto &taskCenter = sicnu::TaskCenter::instance();
      AgentCopilotDockWidget dock( nullptr, taskCenter, prefix, checkpointDir );
      dock.setContext( &dataMgr, nullptr );
      dock.sendPrompt( QStringLiteral( "Attempt request against invalid or insecure endpoint" ) );

      int endpointRefusals = 0;
      for ( const QLabel *lbl : dock.findChildren<QLabel *>() )
      {
        CHECK_FALSE( lbl->text().contains( badProfile.apiKey ) );
        if ( lbl->parentWidget() && lbl->parentWidget()->objectName() == QLatin1String( "agentBubble" ) )
          CHECK_FALSE( lbl->text().contains( genericError ) );
        if ( lbl->text() == expectedReason ) ++endpointRefusals;
      }
      CHECK( endpointRefusals == 1 );
      CHECK_FALSE( dock.isRunActive() );

      const auto piProcs = dock.findChildren<PiProcess *>();
      REQUIRE_FALSE( piProcs.isEmpty() );
      for ( const auto *proc : piProcs )
        CHECK( ( proc->state() == PiProcess::State::Idle || proc->state() == PiProcess::State::Closed ) );

      // Correcting Settings works without clearing the refused conversation.
      QPushButton *sendBtn = nullptr;
      for ( QPushButton *btn : dock.findChildren<QPushButton *>() )
        if ( btn->toolTip().contains( QStringLiteral( "Ctrl+Enter" ) ) ) sendBtn = btn;
      REQUIRE( sendBtn != nullptr );
      CHECK( sendBtn->isEnabled() );
      badProfile.baseUrl = QStringLiteral( "http://127.0.0.1:11434/v1" );
      LlmConfigManager::setActiveProfile( badProfile );
      dock.sendPrompt( QStringLiteral( "Retry after switching to loopback profile" ) );
      CHECK( dock.isRunActive() );
      REQUIRE( until( [&] { return !dock.isRunActive(); }, 10000 ) );
    }
  }
}
