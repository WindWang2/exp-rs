#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>

namespace
{

void ensureApp()
{
  if ( !QCoreApplication::instance() )
  {
    static int argc = 1;
    static char name[] = "test_ui_language_startup";
    static char *argv[] = { name, nullptr };
    new QCoreApplication( argc, argv );
  }
}

void stopOwnedChild( QProcess &child )
{
  if ( child.state() == QProcess::NotRunning )
    return;
  child.terminate();
  if ( !child.waitForFinished( 5000 ) )
  {
    child.kill();
    child.waitForFinished( 5000 );
  }
}

} // namespace

TEST_CASE( "Desktop startup reads the saved language from its QGIS profile", "[i18n][startup][process]" )
{
  ensureApp();
  const bool chinese = GENERATE( false, true );
  const QString expected = chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" );
  INFO( "Saved profile language: " << expected.toStdString() );
  REQUIRE( QFileInfo::exists( QStringLiteral( SICNU_TEST_DESKTOP_EXECUTABLE ) ) );
  for ( const char *catalog : { "sicnu_zh_CN.qm", "qtbase_zh_CN.qm" } )
    REQUIRE( QFileInfo::exists( QDir( QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR ) ).filePath( QString::fromLatin1( catalog ) ) ) );

  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  const QString profileRoot = workspace.filePath( QStringLiteral( "中文 profile \U0001F600" ) );
  const QString profiles = QDir( profileRoot ).filePath( QStringLiteral( "profiles" ) );
  const QString profile = QDir( profiles ).filePath( QStringLiteral( "default" ) );
  REQUIRE( QDir().mkpath( QDir( profile ).filePath( QStringLiteral( "SICNU" ) ) ) );
  {
    QSettings selection( QDir( profiles ).filePath( QStringLiteral( "profiles.ini" ) ), QSettings::IniFormat );
    selection.setValue( QStringLiteral( "core/defaultProfile" ), QStringLiteral( "default" ) );
    selection.sync();
    REQUIRE( selection.status() == QSettings::NoError );
  }
  const QString profileSettings = QDir( profile ).filePath( QStringLiteral( "SICNU/SICNU GEO RS.ini" ) );
  {
    // An explicit temporary INI writes the real profile layout without
    // changing QSettings defaults in either the harness or the desktop child.
    QSettings saved( profileSettings, QSettings::IniFormat );
    saved.setValue( QStringLiteral( "preferences/uiLanguage" ), expected );
    saved.sync();
    REQUIRE( saved.status() == QSettings::NoError );
  }

  auto environment = QProcessEnvironment::systemEnvironment();
  environment.remove( QStringLiteral( "SICNU_LANG" ) );
  environment.remove( QStringLiteral( "QT_LOGGING_RULES" ) );
  environment.remove( QStringLiteral( "QGIS_AUTH_DB_URI" ) );
  const QString authentication = workspace.filePath( QStringLiteral( "auth" ) );
  REQUIRE( QDir().mkpath( authentication ) );
  environment.insert( QStringLiteral( "QGIS_AUTH_DB_DIR_PATH" ), authentication );
  environment.insert( QStringLiteral( "QT_QPA_PLATFORM" ), QStringLiteral( "offscreen" ) );
  environment.insert( QStringLiteral( "QGIS_CUSTOM_CONFIG_PATH" ), profileRoot );
  environment.insert( QStringLiteral( "XDG_CONFIG_HOME" ), QDir( profileRoot ).filePath( QStringLiteral( "native-config" ) ) );
  environment.insert( QStringLiteral( "SICNU_TRANSLATIONS_DIR" ), QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR ) );
  environment.insert( QStringLiteral( "SICNU_LLM_KEYCHAIN" ), QStringLiteral( "0" ) );
  environment.insert( QStringLiteral( "SICNU_OFFLINE" ), QStringLiteral( "1" ) );

  QProcess child;
  const auto cleanup = qScopeGuard( [&] { stopOwnedChild( child ); } );
  child.setProcessEnvironment( environment );
  child.setProcessChannelMode( QProcess::MergedChannels );
  child.setWorkingDirectory( workspace.path() );
  child.start( QStringLiteral( SICNU_TEST_DESKTOP_EXECUTABLE ), QStringList{} );
  REQUIRE( child.waitForStarted( 10000 ) );

  const QRegularExpression signal( QStringLiteral( "UI language:\\s*\"?(zh_CN|en)\"?" ) );
  QByteArray output;
  QString observed;
  QElapsedTimer deadline;
  deadline.start();
  while ( deadline.elapsed() < 60000 )
  {
    child.waitForReadyRead( 100 );
    output += child.readAll();
    const auto match = signal.match( QString::fromUtf8( output ) );
    if ( match.hasMatch() )
    {
      observed = match.captured( 1 );
      break;
    }
    if ( child.state() == QProcess::NotRunning )
      break;
  }
  // Shut down only this QProcess before reporting any failed assertion.
  stopOwnedChild( child );
  INFO( "Observed startup language: " << observed.toStdString() );
  INFO( "Child process error: " << child.errorString().toStdString() );
  CHECK( child.state() == QProcess::NotRunning );
  for ( const QString &directory : QDir( workspace.path() ).entryList( QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System ) )
  {
    INFO( "Created outer directory: " << directory.toStdString() );
    CHECK_FALSE( directory.contains( QChar( QChar::ReplacementCharacter ) ) );
  }
  REQUIRE_FALSE( observed.isEmpty() );
  CHECK( observed == expected );
  QSettings persisted( profileSettings, QSettings::IniFormat );
  CHECK( persisted.status() == QSettings::NoError );
  CHECK( persisted.contains( QStringLiteral( "preferences/uiLanguage" ) ) );
  CHECK( persisted.value( QStringLiteral( "preferences/uiLanguage" ) ).toString() == expected );
}
