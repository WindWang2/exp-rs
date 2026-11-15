#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "agent/agent_copilot_dock_widget.h"
#include "agent/llm_settings_dialog.h"
#include "app/panels/data_manager_panel.h"
#include "app/shell/rs_job_panel.h"
#include "app/application_language.h"
#include "support/application_translations.h"
#include "support/qsettings_redirect.h"
#include "support/qt_lifecycle.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSettings>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>

#include <qgsabstractcontentcache.h>
#include <qgsprojectstoredobjectmanagermodel.h>

#include <qgsapplication.h>

#include <utility>

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace {

exprs_test::QSettingsUserRootRedirect settingsRedirect;

void ensureQtApp()
{
  // All model configuration and secrets belong to this isolated fixture.
  qputenv( "SICNU_LLM_KEYCHAIN", "0" );
  QSettings::setDefaultFormat( QSettings::IniFormat );
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "bilingual-delivery-ui" ) );
  if ( !QApplication::instance() )
  {
    QCoreApplication::setAttribute( Qt::AA_DontUseNativeDialogs );
    static int argc = 1;
    static char appName[] = "test_bilingual_delivery_ui";
    static char *argv[] = { appName, nullptr };
    // Use the same QGIS widget/provider lifecycle as the Data Manager fixture.
    new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
  }
  sicnu::agent::LlmProviderProfile profile;
  profile.id = QStringLiteral( "bilingual-loopback" );
  profile.name = QStringLiteral( "My model profile" );
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:9/v1" );
  profile.modelName = QStringLiteral( "fixture" );
  sicnu::agent::LlmConfigManager::setActiveProfile( profile );
}

struct ScopedEnvironment
{
  const char *name;
  bool wasSet;
  QByteArray previous;

  ScopedEnvironment( const char *key, const QByteArray &value )
    : name( key ), wasSet( qEnvironmentVariableIsSet( key ) ), previous( qgetenv( key ) )
  {
    qputenv( key, value );
  }
  ~ScopedEnvironment()
  {
    if ( wasSet )
      qputenv( name, previous );
    else
      qunsetenv( name );
  }
};

struct LanguageSession
{
  ScopedEnvironment languageOverride{ "SICNU_LANG", {} };
  ScopedEnvironment translationDirectory{ "SICNU_TRANSLATIONS_DIR", SICNU_TEST_TRANSLATIONS_DIR };
  QString applied;

  explicit LanguageSession( bool chinese )
  {
    ensureQtApp();
    exprs_test::removeApplicationTranslators();
    QSettings().setValue( QStringLiteral( "preferences/uiLanguage" ),
                          chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) );
    applied = SicnuUi::installUiTranslations( *qApp );
  }
  ~LanguageSession()
  {
    exprs_test::removeApplicationTranslators();
  }
};

QString translated( const char *context, const char *source, bool chinese )
{
  const QString result = QCoreApplication::translate( context, source );
  INFO( context << " / " << source );
  if ( chinese )
    CHECK( result != QString::fromUtf8( source ) );
  else
    CHECK( result == QString::fromUtf8( source ) );
  return result;
}

bool containsHan( const QString &text )
{
  static const QRegularExpression expression( QStringLiteral( "\\p{Han}" ) );
  return expression.match( text ).hasMatch();
}

QPushButton *buttonWithText( QWidget &parent, const QString &text )
{
  for ( auto *button : parent.findChildren<QPushButton *>() )
    if ( button->text() == text )
      return button;
  return nullptr;
}

} // namespace

TEST_CASE( "Delivery widgets use compiled application and Qt translations", "[ui][i18n][delivery]" )
{
  const bool chinese = GENERATE( false, true );
  LanguageSession language( chinese );
  REQUIRE( language.applied == ( chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) ) );
  if ( chinese )
  {
    REQUIRE( QFileInfo::exists( QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR "/sicnu_zh_CN.qm" ) ) );
    REQUIRE( qApp->findChild<QTranslator *>( QStringLiteral( "sicnuAppTranslator" ) ) );
    REQUIRE( qApp->findChild<QTranslator *>( QStringLiteral( "sicnuQtTranslator" ) ) );
  }

  // lupdate attributes these template calls to T, but runtime lookup uses
  // the inherited Q_OBJECT base's staticMetaObject.
  const QString renameLayout = QgsProjectStoredObjectManagerModelBase::tr( "Rename Layout" );
  CHECK( renameLayout == ( chinese ? QStringLiteral( "重命名布局" ) : QStringLiteral( "Rename Layout" ) ) );
  const QString requestFailure = QgsAbstractContentCacheBase::tr( "%3 request failed [error: %1 - url: %2]" );
  CHECK( containsHan( requestFailure ) == chinese );

  sicnu::agent::AgentCopilotDockWidget dock;
  const char *agentContext = dock.metaObject()->className();
  CHECK( dock.windowTitle() == translated( agentContext, "AI Copilot Assistant", chinese ) );
  REQUIRE( buttonWithText( dock, translated( agentContext, "Settings", chinese ) ) );
  REQUIRE( buttonWithText( dock, translated( agentContext, "Clear conversation", chinese ) ) );
  REQUIRE( buttonWithText( dock, translated( agentContext, "Send", chinese ) ) );
  const auto *profiles = dock.findChild<QComboBox *>();
  REQUIRE( profiles );
  CHECK( profiles->currentText() == QStringLiteral( "My model profile" ) );

  const QString prompt = QStringLiteral( "NDVI <mask>" );
  dock.sendPrompt( prompt );
  const auto *userMessage = dock.findChild<QLabel *>( QStringLiteral( "agentUserMessage" ) );
  REQUIRE( userMessage );
  CHECK( userMessage->text() == translated( agentContext, "<b>You:</b> %1", chinese ).arg( prompt.toHtmlEscaped() ) );
  CHECK( dock.messageHistory().at( 1 ).toObject().value( QStringLiteral( "content" ) ).toString() == prompt );

  sicnu::agent::LlmSettingsDialog settings;
  CHECK( settings.windowTitle() == translated( settings.metaObject()->className(),
                                              "AI Copilot Model & Service Settings", chinese ) );
  const auto *buttons = settings.findChild<QDialogButtonBox *>();
  REQUIRE( buttons );
  for ( const auto standard : { QDialogButtonBox::Ok, QDialogButtonBox::Cancel } )
  {
    const auto *button = buttons->button( standard );
    REQUIRE( button );
    CHECK( containsHan( button->text() ) == chinese );
    if ( !chinese )
      CHECK( QString( button->text() ).remove( QLatin1Char( '&' ) )
             == ( standard == QDialogButtonBox::Ok ? QStringLiteral( "OK" ) : QStringLiteral( "Cancel" ) ) );
  }

  sicnu::DataManagerPanel data( nullptr );
  CHECK( data.windowTitle() == translated( "QObject", "Data Management", chinese ) );
  auto *previousPage = data.findChild<QToolButton *>( QStringLiteral( "dataManagerPagerPrev" ) );
  auto *nextPage = data.findChild<QToolButton *>( QStringLiteral( "dataManagerPagerNext" ) );
  REQUIRE( previousPage );
  REQUIRE( nextPage );
  CHECK( previousPage->text() == translated( "QObject", "Previous Page", chinese ) );
  CHECK( nextPage->text() == translated( "QObject", "Next Page", chinese ) );
}

TEST_CASE( "Approval language preserves the proposed script and parameters", "[ui][i18n][approval]" )
{
  const bool chinese = GENERATE( false, true );
  LanguageSession language( chinese );
  REQUIRE( language.applied == ( chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) ) );
  sicnu::agent::AgentCopilotDockWidget dock;
  const QJsonObject params{
    { QStringLiteral( "script" ), QStringLiteral( "result['label'] = 'My NDVI result'" ) },
    { QStringLiteral( "output" ), QStringLiteral( "C:/analysis/ndvi_change_mask.tif" ) } };
  const QJsonObject plan{ { QStringLiteral( "steps" ), QJsonArray{ QJsonObject{
    { QStringLiteral( "id" ), QStringLiteral( "my_step" ) },
    { QStringLiteral( "name" ), QStringLiteral( "python:run_script" ) },
    { QStringLiteral( "params" ), params } } } } };
  const QJsonObject envelope{
    { QStringLiteral( "id" ), QStringLiteral( "approval-language" ) },
    { QStringLiteral( "function" ), QJsonObject{
      { QStringLiteral( "name" ), QStringLiteral( "approval_fixture" ) },
      { QStringLiteral( "arguments" ), plan } } } };
  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed", Qt::DirectConnection,
                                     Q_ARG( QJsonObject, envelope ) ) );
  const char *context = dock.metaObject()->className();
  auto *review = dock.findChild<QPushButton *>( QStringLiteral( "agentReviewPlanParameters" ) );
  auto *parameters = dock.findChild<QTextEdit *>( QStringLiteral( "agentPlanParameters" ) );
  REQUIRE( review );
  REQUIRE( parameters );
  CHECK( review->text() == translated( context, "Review parameters", chinese ) );
  REQUIRE( buttonWithText( dock, translated( context, "Confirm & run", chinese ) ) );
  REQUIRE( buttonWithText( dock, translated( context, "Preview on canvas", chinese ) ) );
  CHECK( parameters->isReadOnly() );
  CHECK( parameters->isHidden() );
  review->click();
  CHECK_FALSE( parameters->isHidden() );
  const auto reviewed = QJsonDocument::fromJson( parameters->toPlainText().toUtf8() );
  CHECK( reviewed.object() == plan );
  CHECK( envelope.value( QStringLiteral( "function" ) ).toObject().value( QStringLiteral( "arguments" ) ).toObject() == plan );
}

TEST_CASE( "Task Center translates canonical titles while retaining custom job labels", "[ui][i18n][task_center]" )
{
  const bool chinese = GENERATE( false, true );
  LanguageSession language( chinese );
  REQUIRE( language.applied == ( chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) ) );
  RsJobPanel panel;
  auto *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "rsJobTree" ) );
  REQUIRE( tree );
  tree->clear();
  const std::pair<const char *, const char *> canonicalTitles[] = {
    { "python:environment", "Python environment" }, { "python:run_script", "Run Python script" },
    { "io:inspect", "Inspect Dataset" }, { "io:verify_dataset", "Verify Dataset" }, { "io:doctor", "Data Doctor" } };
  long taskId = 900000;
  for ( const auto &[id, title] : canonicalTitles )
  {
    sicnu::AlgorithmTaskInfo info;
    info.taskId = ++taskId;
    info.algorithmId = QString::fromUtf8( id );
    info.algorithmName = QString::fromUtf8( title );
    info.hasJobRequest = true;
    info.jobRequest.algorithmId = id;
    info.jobRequest.title = title;
    info.status = sicnu::TaskStatus::Completed;
    panel.onTaskAdded( info );
    auto *item = tree->topLevelItem( 0 );
    REQUIRE( item );
    CHECK( item->text( 0 ) == translated( "RsJobPanel", title, chinese ) );
    CHECK( info.algorithmId == QString::fromUtf8( id ) );
    CHECK( info.jobRequest.algorithmId == id );
    CHECK( info.jobRequest.title == title );

    info.jobRequest.title = "My NDVI change check";
    panel.onTaskAdded( info );
    CHECK( item->text( 0 ) == QStringLiteral( "My NDVI change check" ) );
    info.hasJobRequest = false;
    info.algorithmName = QStringLiteral( "我的 NDVI 检查" );
    panel.onTaskAdded( info );
    CHECK( item->text( 0 ) == QStringLiteral( "我的 NDVI 检查" ) );
  }

  sicnu::AlgorithmTaskInfo customOperator;
  customOperator.taskId = ++taskId;
  customOperator.algorithmId = QStringLiteral( "custom:inspect" );
  customOperator.algorithmName = QStringLiteral( "Inspect Dataset" );
  customOperator.status = sicnu::TaskStatus::Completed;
  panel.onTaskAdded( customOperator );
  CHECK( tree->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "Inspect Dataset" ) );

  // Exercise the actual destructive-action dialog, choosing No to leave the
  // task authority untouched. Its standard buttons come from the Qt catalog.
  bool observed = false;
  QString yesText;
  QString noText;
  bool defaultIsNo = false;
  QTimer dismiss;
  QObject::connect( &dismiss, &QTimer::timeout, &panel, [&] {
    auto *message = qobject_cast<QMessageBox *>( QApplication::activeModalWidget() );
    if ( !message )
      return;
    observed = true;
    yesText = message->button( QMessageBox::Yes )->text();
    noText = message->button( QMessageBox::No )->text();
    defaultIsNo = message->defaultButton() == message->button( QMessageBox::No );
    message->done( QMessageBox::No );
  } );
  auto *clear = panel.findChild<QPushButton *>( QStringLiteral( "rsJobClearBtn" ) );
  REQUIRE( clear );
  dismiss.start( 0 );
  clear->click();
  dismiss.stop();
  REQUIRE( observed );
  CHECK( defaultIsNo );
  CHECK( containsHan( yesText ) == chinese );
  CHECK( containsHan( noText ) == chinese );
  CHECK( tree->topLevelItemCount() == 6 );
}
