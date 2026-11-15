#include <catch2/catch_test_macros.hpp>

#include "app/application_language.h"
#include "support/qsettings_redirect.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTranslator>

namespace
{
exprs_test::QSettingsUserRootRedirect settingsRedirect;

void ensureApp()
{
  QSettings::setDefaultFormat( QSettings::IniFormat );
  if ( !QCoreApplication::instance() )
  {
    static int argc = 1;
    static char name[] = "test_ui_language";
    static char *argv[] = { name, nullptr };
    new QCoreApplication( argc, argv );
  }
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "ui-language" ) );
}

struct TranslationScope
{
  const QByteArray previousOverride = qgetenv( "SICNU_LANG" );
  const bool hadOverride = qEnvironmentVariableIsSet( "SICNU_LANG" );
  TranslationScope()
  {
    ensureApp();
    qunsetenv( "SICNU_LANG" );
    QSettings().clear();
  }
  ~TranslationScope()
  {
    for ( const char *name : { "sicnuAppTranslator", "sicnuQtTranslator" } )
      if ( auto *translator = qApp->findChild<QTranslator *>( QString::fromLatin1( name ) ) )
      {
        qApp->removeTranslator( translator );
        delete translator;
      }
    hadOverride ? qputenv( "SICNU_LANG", previousOverride ) : qunsetenv( "SICNU_LANG" );
  }
};

const QString catalogs = QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR );
} // namespace

TEST_CASE( "UI language resolves override, saved preference and system in order", "[i18n][language]" )
{
  using SicnuUi::resolveUiLanguage;
  CHECK( resolveUiLanguage( "en", "zh_CN", "en_US" ) == "zh_CN" );
  CHECK( resolveUiLanguage( "zh_CN", "en", "zh_CN" ) == "en" );
  CHECK( resolveUiLanguage( "en", "", "zh_CN" ) == "en" );
  CHECK( resolveUiLanguage( "zh_CN", "", "en_US" ) == "zh_CN" );
  CHECK( resolveUiLanguage( "system", "", "zh_CN" ) == "zh_CN" );
  CHECK( resolveUiLanguage( "invalid", "", "en_GB" ) == "en" );
  CHECK( resolveUiLanguage( "invalid", "", "zh_CN" ) == "zh_CN" );
  CHECK( resolveUiLanguage( "system", "", "fr_FR" ) == "en" );
  CHECK( resolveUiLanguage( "en", "zh-CN", "en_US" ) == "zh_CN" );
  CHECK( resolveUiLanguage( "zh_CN", "invalid", "zh_CN" ) == "en" );
  CHECK( resolveUiLanguage( "zh_CN", "system", "en_US" ) == "en" );
  CHECK( resolveUiLanguage( "en", "system", "zh_CN" ) == "zh_CN" );
  CHECK( resolveUiLanguage( "system", "", "zh-Hans-CN" ) == "zh_CN" );
}

TEST_CASE( "Chinese startup installs the actual application and Qt catalogs", "[i18n][language]" )
{
  TranslationScope scope;
  QSettings().setValue( "preferences/uiLanguage", "zh_CN" );
  REQUIRE( SicnuUi::installUiTranslations( *qApp, { catalogs } ) == "zh_CN" );
  CHECK( QCoreApplication::translate( "sicnu::agent::AgentCopilotDockWidget", "Review parameters" )
         == QStringLiteral( "查看执行参数" ) );
  CHECK( QCoreApplication::translate( "QPlatformTheme", "Cancel" ).contains( QStringLiteral( "取消" ) ) );
  CHECK( QSettings().value( "preferences/uiLanguage" ).toString() == "zh_CN" );
}

TEST_CASE( "English startup uses source labels even on a Chinese host", "[i18n][language]" )
{
  TranslationScope scope;
  QSettings().setValue( "preferences/uiLanguage", "en" );
  REQUIRE( SicnuUi::installUiTranslations( *qApp, { catalogs } ) == "en" );
  CHECK( QCoreApplication::translate( "sicnu::agent::AgentCopilotDockWidget", "Review parameters" )
         == QStringLiteral( "Review parameters" ) );
  CHECK( QCoreApplication::translate( "QPlatformTheme", "Cancel" ) == QStringLiteral( "Cancel" ) );
  CHECK( qApp->findChild<QTranslator *>( "sicnuQtTranslator" ) == nullptr );
}

TEST_CASE( "Launch override does not rewrite the saved language", "[i18n][language]" )
{
  TranslationScope scope;
  QSettings().setValue( "preferences/uiLanguage", "en" );
  qputenv( "SICNU_LANG", "zh_CN" );
  CHECK( SicnuUi::installUiTranslations( *qApp, { catalogs } ) == "zh_CN" );
  CHECK( QSettings().value( "preferences/uiLanguage" ).toString() == "en" );
}

TEST_CASE( "Incomplete translation bundles fall back to English together", "[i18n][language]" )
{
  TranslationScope scope;
  QSettings().setValue( "preferences/uiLanguage", "zh_CN" );
  QTemporaryDir incomplete;
  REQUIRE( incomplete.isValid() );
  SECTION( "application catalog missing" )
  {
    REQUIRE( QFile::copy( catalogs + "/qtbase_zh_CN.qm", incomplete.filePath( "qtbase_zh_CN.qm" ) ) );
  }
  SECTION( "Qt catalog missing" )
  {
    REQUIRE( QFile::copy( catalogs + "/sicnu_zh_CN.qm", incomplete.filePath( "sicnu_zh_CN.qm" ) ) );
  }
  CHECK( SicnuUi::installUiTranslations( *qApp, { incomplete.path() } ) == "en" );
  CHECK( QCoreApplication::translate( "sicnu::agent::AgentCopilotDockWidget", "Review parameters" )
         == QStringLiteral( "Review parameters" ) );
  CHECK( QCoreApplication::translate( "QPlatformTheme", "Cancel" ) == QStringLiteral( "Cancel" ) );
}
