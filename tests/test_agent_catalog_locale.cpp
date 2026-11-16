#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/agent_copilot_dock_widget.h"
#include "agent/interaction_tool_registry.h"
#include "agent/runtime/agent_tool_host.h"
#include "agent/runtime/conversation_checkpoint_store.h"
#include "app/application_language.h"
#include "app/main_window.h"
#include "app/project_context.h"
#include "geospatial/remote/offline_gate.h"
#include "processing/framework/algorithm_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/provider_algorithm_adapter.h"
#include "processing/framework/task_center.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QTranslator>

#include <qgsapplication.h>
#include <json/json.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>

using sicnu::agent::runtime::ConversationCheckpointBinding;
using sicnu::agent::runtime::ConversationCheckpointStore;

namespace
{
void requireChild( bool condition, const char *code )
{
  if ( !condition ) throw std::runtime_error( code );
}

QString digest( const QByteArray &bytes )
{
  return QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

QByteArray readFile( const QString &path )
{
  QFile file( path );
  requireChild( file.open( QIODevice::ReadOnly ), "fixture_file_unreadable" );
  return file.readAll();
}

QJsonObject qtSchema( const Json::Value &schema )
{
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  return QJsonDocument::fromJson( QByteArray::fromStdString( Json::writeString( writer, schema ) ) ).object();
}

ConversationCheckpointBinding binding( const QString &revision )
{
  return { QStringLiteral( "locale-project" ), QStringLiteral( "locale-conversation" ),
    QString( 64, QLatin1Char( 'a' ) ), revision, QString( 64, QLatin1Char( 'b' ) ) };
}

// This is a Qt-store binding fixture, not an SDK session/entry validation probe.
QByteArray checkpoint( const ConversationCheckpointBinding &ids )
{
  return QJsonDocument( QJsonObject{
    { "schema", "sicnu.pi-checkpoint/1" }, { "piVersion", "1.5.2" },
    { "checkpointId", "locale-checkpoint" }, { "completedRunId", "locale-run" },
    { "projectId", ids.projectId }, { "conversationId", ids.conversationId },
    { "sdkReceiptSha256", ids.sdkReceiptSha256 }, { "toolRevision", ids.toolRevision },
    { "systemPromptSha256", ids.systemPromptSha256 }, { "status", "completed" },
    { "header", QJsonObject{ { "type", "session" }, { "version", 3 }, { "id", "locale-sdk-fixture" } } },
    { "entries", QJsonArray{ QJsonObject{ { "type", "message" }, { "id", "locale-entry" },
      { "message", QJsonObject{ { "role", "user" }, { "content", "Locale binding fixture" } } } } } },
    { "leafId", "locale-entry" }
  } ).toJson( QJsonDocument::Compact );
}

int childProbe( const QString &language, const QString &root, const QString &output )
{
  try
  {
    requireChild( language == "en" || language == "zh_CN", "invalid_fixture_language" );
    const QString profile = QDir( root ).filePath( language + "/profile" );
    requireChild( QDir().mkpath( profile ), "profile_directory_failed" );
    qunsetenv( "SICNU_LANG" );
    qputenv( "SICNU_LLM_KEYCHAIN", "0" );
    qputenv( "SICNU_WORKER_EXECUTION", "off" );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, profile );
    QCoreApplication::setOrganizationName( QStringLiteral( "SICNU" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "SICNU GEO RS" ) );
    static int argc = 1;
    static char name[] = "test_agent_catalog_locale_child";
    static char *argv[] = { name, nullptr };
    // The desktop has the same process-lifetime QApplication/QGIS ownership.
    auto *app = new QgsApplication( argc, argv, true, profile );
    QCoreApplication::setOrganizationName( QStringLiteral( "SICNU" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "SICNU GEO RS" ) );
    QgsApplication::setPrefixPath( QCoreApplication::applicationDirPath(), true );
    QgsApplication::init( profile );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, profile );
    QSettings saved;
    requireChild( QFileInfo( saved.fileName() ).absoluteFilePath().startsWith( profile + '/' ), "settings_escaped_fixture_profile" );
    saved.setValue( "preferences/uiLanguage", language );
    saved.setValue( "agent/conversationBackend", "native" );
    saved.sync();
    requireChild( saved.status() == QSettings::NoError, "fixture_settings_failed" );
    for ( const auto &name : { "sicnu_zh_CN.qm", "qtbase_zh_CN.qm" } )
      requireChild( QFileInfo::exists( QDir( SICNU_TEST_TRANSLATIONS_DIR ).filePath( name ) ), "real_catalog_missing" );
    const QString applied = SicnuUi::installUiTranslations( *app, { QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR ) } );
    requireChild( applied == language, "saved_language_not_applied" );
    const bool realChineseCatalogs = app->findChild<QTranslator *>( "sicnuAppTranslator" )
      && app->findChild<QTranslator *>( "sicnuQtTranslator" );
    requireChild( realChineseCatalogs == ( language == "zh_CN" ), "translator_state_mismatch" );
    sicnu::geo::offline::setEnabled( true );
    sicnu::geo::offline::applyGdalNetworkDeny();
    QgsApplication::initQgis();
    sicnu::AlgorithmEngine::instance().initialize();

    // No synthetic host: the product shell constructs and binds its own Dock.
    auto window = std::make_unique<QgisDesktopWindow>();
    window->initLayerTree();
    auto *dock = window->findChild<sicnu::agent::AgentCopilotDockWidget *>();
    requireChild( dock != nullptr, "actual_shell_dock_missing" );
    auto *host = dock->findChild<sicnu::agent::runtime::AgentToolHost *>();
    requireChild( host != nullptr, "actual_dock_host_missing" );
    const auto snapshot = host->catalogSnapshot();
    requireChild( snapshot.error.isEmpty() && !snapshot.tools.isEmpty(), "actual_host_catalog_unavailable" );
    requireChild( !dock->isRunActive(), "unexpected_conversation_run" );
    requireChild( window->projectContext() != nullptr, "actual_project_context_missing" );
    requireChild( sicnu::TaskCenter::instance().allTasks().isEmpty(), "unexpected_native_task" );
    requireChild( window->projectContext()->dataManager().assets().isEmpty(), "unexpected_data_asset" );

    auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
    QJsonObject rawSchemas, rawEnums;
    int providerCount = 0;
    for ( const auto &descriptor : registry.listDescriptors() )
    {
      const auto adapter = registry.findAdapter( descriptor.id );
      if ( !dynamic_cast<sicnu::processing::ProviderAlgorithmAdapter *>( adapter.get() ) ) continue;
      ++providerCount;
      const QString id = QString::fromStdString( descriptor.id );
      rawSchemas.insert( id, qtSchema( descriptor.toInputSchema() ) );
      for ( const auto &port : descriptor.inputs )
        if ( !port.enumOptions.empty() )
        {
          QJsonArray options;
          for ( const auto &option : port.enumOptions ) options.append( QString::fromStdString( option ) );
          rawEnums.insert( id + '/' + QString::fromStdString( port.name ), options );
        }
    }
    requireChild( providerCount > 0 && !rawEnums.isEmpty(), "real_provider_enum_coverage_missing" );
    QJsonArray selectedIds;
    for ( const auto &entry : snapshot.tools )
    {
      const QString id = entry.toObject().value( "canonicalId" ).toString();
      selectedIds.append( id );
      const auto adapter = registry.findAdapter( id.toStdString() );
      requireChild( !dynamic_cast<sicnu::processing::ProviderAlgorithmAdapter *>( adapter.get() ), "localized_provider_leaked_into_selected_catalog" );
    }
    for ( const auto &id : { "python:run_script", "python:environment", "rs:spectral_index", "view:get_state" } )
      requireChild( selectedIds.contains( QString::fromLatin1( id ) ), "core_tool_disappeared" );

    const auto ids = binding( snapshot.revision );
    ConversationCheckpointStore store( QDir( root ).filePath( "shared-checkpoint" ) );
    if ( language == "en" )
    {
      const auto written = store.saveCompleted( ids, checkpoint( ids ) );
      requireChild( written.committed && written.error.isEmpty(), "english_binding_save_failed" );
    }
    const auto restored = store.load( ids );
    requireChild( restored.found && restored.error.isEmpty() && restored.checkpointJson == checkpoint( ids ), "cross_process_binding_restore_failed" );

    // A real semantic enum change must still invalidate both admission and
    // the durable binding; this guards against stripping every enum field.
    sicnu::agent::InteractionToolDefinition tool;
    tool.name = "view:get_state"; // Existing priority-one tool cannot fall outside the 64-tool cap.
    tool.description = "Enum identity fixture";
    tool.inputSchema["type"] = "object";
    tool.inputSchema["properties"]["choice"]["type"] = "string";
    tool.inputSchema["properties"]["choice"]["enum"].append( "first" );
    tool.inputSchema["properties"]["choice"]["enum"].append( "second" );
    tool.handler = []( const Json::Value & ) { return Json::Value( Json::objectValue ); };
    auto &interactions = sicnu::agent::InteractionToolRegistry::instance();
    interactions.registerTool( tool );
    const auto before = host->catalogSnapshot();
    requireChild( before.error.isEmpty(), "enum_fixture_catalog_failed" );
    bool enumFixtureSelected = false;
    for ( const auto &entry : before.tools )
    {
      const auto definition = entry.toObject();
      if ( definition.value( "canonicalId" ) != "view:get_state" ) continue;
      const auto choices = definition.value( "parameters" ).toObject().value( "properties" ).toObject()
        .value( "choice" ).toObject().value( "enum" ).toArray();
      enumFixtureSelected = choices == QJsonArray{ "first", "second" };
    }
    requireChild( enumFixtureSelected, "semantic_enum_fixture_not_selected" );
    ConversationCheckpointStore driftStore( QDir( root ).filePath( language + "/enum-drift-checkpoint" ) );
    const auto oldBinding = binding( before.revision );
    const auto oldSaved = driftStore.saveCompleted( oldBinding, checkpoint( oldBinding ) );
    requireChild( oldSaved.committed && oldSaved.error.isEmpty(), "enum_fixture_save_failed" );
    tool.inputSchema["properties"]["choice"]["enum"][1] = "third";
    interactions.registerTool( std::move( tool ) );
    const auto after = host->catalogSnapshot();
    requireChild( after.error.isEmpty() && after.revision != before.revision, "semantic_enum_drift_was_ignored" );
    requireChild( !host->beginRun( QStringLiteral( "stale-enum-run" ), before.revision ), "stale_schema_was_admitted" );
    requireChild( driftStore.load( binding( after.revision ) ).error == "checkpoint_binding_mismatch", "semantic_enum_checkpoint_was_accepted" );
    requireChild( !dock->isRunActive() && sicnu::TaskCenter::instance().allTasks().isEmpty()
      && window->projectContext()->dataManager().assets().isEmpty(), "probe_started_work" );

    const QJsonObject result{
      { "pid", static_cast<double>( QCoreApplication::applicationPid() ) }, { "language", applied },
      { "real_chinese_catalogs", realChineseCatalogs }, { "profile_isolated", true },
      { "raw_provider_count", providerCount }, { "raw_enum_count", rawEnums.size() },
      { "raw_provider_schema_sha256", digest( QJsonDocument( rawSchemas ).toJson( QJsonDocument::Compact ) ) },
      { "raw_provider_enum_sha256", digest( QJsonDocument( rawEnums ).toJson( QJsonDocument::Compact ) ) },
      { "selected_tools", snapshot.tools }, { "selected_ids", selectedIds }, { "revision", snapshot.revision },
      { "checkpoint_sha256", restored.checkpointSha256 }, { "store_binding_restored", true },
      { "semantic_enum_drift_refused", true }, { "prompts_submitted_by_probe", 0 },
      { "run_active", dock->isRunActive() }, { "native_tasks", 0 }, { "data_assets", 0 },
      { "checkpoint_scope", "Qt store binding only; no SDK session was started" }
    };
    QFile report( output );
    requireChild( report.open( QIODevice::WriteOnly | QIODevice::NewOnly ), "fresh_child_report_failed" );
    requireChild( report.write( QJsonDocument( result ).toJson( QJsonDocument::Compact ) ) > 0, "child_report_write_failed" );
    report.close();
    window.reset();
    // QApplication/QGIS remain process-lifetime, matching existing shell tests.
    return 0;
  }
  catch ( const std::exception &error )
  {
    std::fprintf( stderr, "catalog-locale child failed: %s\n", error.what() );
    return 1;
  }
}
} // namespace

TEST_CASE( "Fresh English and Chinese desktop hosts retain one strict checkpoint catalogue identity", "[agent][catalog][locale][process]" )
{
  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  QJsonObject observations;
  for ( const QString language : { QStringLiteral( "en" ), QStringLiteral( "zh_CN" ) } )
  {
    const QString childRoot = workspace.filePath( language );
    REQUIRE( QDir().mkpath( childRoot ) );
    auto environment = QProcessEnvironment::systemEnvironment();
    for ( const QString &key : environment.keys() )
      if ( key.startsWith( "SICNU_" ) || key.startsWith( "EXPRS_" ) || key.startsWith( "NODE_" ) ) environment.remove( key );
    for ( const auto &key : { "HOME", "USERPROFILE", "APPDATA", "LOCALAPPDATA", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "QGIS_CUSTOM_CONFIG_PATH", "QGIS_AUTH_DB_DIR_PATH" } )
      environment.insert( QString::fromLatin1( key ), childRoot );
    environment.remove( "QGIS_AUTH_DB_URI" );
    environment.insert( "QT_QPA_PLATFORM", "offscreen" );
    environment.insert( "SICNU_LLM_KEYCHAIN", "0" );
    environment.insert( "SICNU_WORKER_EXECUTION", "off" );
    environment.insert( "SICNU_OFFLINE", "1" );
    environment.insert( "PROJ_NETWORK", "OFF" );
    const QString output = workspace.filePath( language + ".json" );
    QProcess child;
    const auto cleanup = qScopeGuard( [&] {
      if ( child.state() != QProcess::NotRunning ) { child.kill(); child.waitForFinished( 5000 ); }
    } );
    child.setProcessEnvironment( environment );
    child.setWorkingDirectory( workspace.path() );
    child.setProcessChannelMode( QProcess::MergedChannels );
    child.start( QCoreApplication::applicationFilePath(), { "--locale-child", language, workspace.path(), output } );
    REQUIRE( child.waitForStarted( 10000 ) );
    REQUIRE( child.waitForFinished( 90000 ) );
    INFO( QString::fromUtf8( child.readAll() ).toStdString() );
    REQUIRE( child.exitStatus() == QProcess::NormalExit );
    REQUIRE( child.exitCode() == 0 );
    observations.insert( language, QJsonDocument::fromJson( readFile( output ) ).object() );
  }
  const auto english = observations.value( "en" ).toObject();
  const auto chinese = observations.value( "zh_CN" ).toObject();
  CHECK( english.value( "pid" ) != chinese.value( "pid" ) );
  CHECK( english.value( "language" ) == "en" );
  CHECK( chinese.value( "language" ) == "zh_CN" );
  CHECK( chinese.value( "real_chinese_catalogs" ).toBool() );
  CHECK( english.value( "raw_provider_count" ).toInt() > 0 );
  CHECK( english.value( "raw_provider_count" ) == chinese.value( "raw_provider_count" ) );
  CHECK( english.value( "raw_provider_schema_sha256" ) != chinese.value( "raw_provider_schema_sha256" ) );
  CHECK( english.value( "raw_provider_enum_sha256" ) != chinese.value( "raw_provider_enum_sha256" ) );
  CHECK( english.value( "selected_tools" ) == chinese.value( "selected_tools" ) );
  CHECK( english.value( "selected_ids" ) == chinese.value( "selected_ids" ) );
  CHECK( english.value( "revision" ) == chinese.value( "revision" ) );
  CHECK( english.value( "checkpoint_sha256" ) == chinese.value( "checkpoint_sha256" ) );
  for ( const auto &entry : { english, chinese } )
  {
    CHECK( entry.value( "store_binding_restored" ).toBool() );
    CHECK( entry.value( "semantic_enum_drift_refused" ).toBool() );
    CHECK_FALSE( entry.value( "run_active" ).toBool() );
    CHECK( entry.value( "prompts_submitted_by_probe" ).toInt() == 0 );
    CHECK( entry.value( "native_tasks" ).toInt() == 0 );
    CHECK( entry.value( "data_assets" ).toInt() == 0 );
  }
}

int main( int argc, char **argv )
{
  if ( argc == 5 && std::strcmp( argv[1], "--locale-child" ) == 0 )
    return childProbe( QString::fromUtf8( argv[2] ), QString::fromUtf8( argv[3] ), QString::fromUtf8( argv[4] ) );
  QCoreApplication application( argc, argv );
  return Catch::Session().run( argc, argv );
}
