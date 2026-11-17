#include "application_language.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

#include <memory>

namespace
{

// lupdate attributes inherited tr() calls in these QGIS templates to T,
// but Qt resolves them through their Q_OBJECT bases. Keep the runtime
// contexts extractable when updating catalogs with -no-obsolete, without
// changing the shared QGIS template headers or their runtime lookups.
[[maybe_unused]] constexpr const char *kRuntimeTemplateTranslationAnchors[] = {
  QT_TRANSLATE_NOOP( "QgsAbstractContentCacheBase", "%3 request failed [error: %1 - url: %2]" ),
  QT_TRANSLATE_NOOP( "QgsAbstractContentCacheBase", "%4 request error [status: %1 - reason phrase: %2] for %3" ),
  QT_TRANSLATE_NOOP( "QgsProjectStoredObjectManagerModelBase", "Rename Layout" ),
  QT_TRANSLATE_NOOP( "QgsProjectStoredObjectManagerModelBase", "There is already a layout named “%1”." )
};

} // namespace

namespace SicnuUi
{

QString resolveUiLanguage( const QString &preference, const QString &launchOverride,
                           const QString &systemLanguage )
{
  QString requested = launchOverride.trimmed();
  if ( requested.isEmpty() )
    requested = preference == QLatin1String( "zh_CN" ) || preference == QLatin1String( "en" )
                  ? preference : systemLanguage;
  else if ( requested == QLatin1String( "system" ) )
    requested = systemLanguage;
  requested.replace( QLatin1Char( '-' ), QLatin1Char( '_' ) );
  return requested.compare( QLatin1String( "zh" ), Qt::CaseInsensitive ) == 0
             || requested.startsWith( QLatin1String( "zh_" ), Qt::CaseInsensitive )
           ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" );
}

QString installUiTranslations( QCoreApplication &application, const QStringList &directories )
{
  const QLocale systemLocale = QLocale::system();
  const QString systemLanguage = systemLocale.uiLanguages().value( 0, systemLocale.name() );
  const QString language = resolveUiLanguage(
    QSettings().value( QStringLiteral( "preferences/uiLanguage" ), QStringLiteral( "system" ) ).toString(),
    qEnvironmentVariable( "SICNU_LANG" ), systemLanguage );
  if ( language == QLatin1String( "en" ) )
    return language;

  const QStringList candidates = directories.isEmpty()
                                  ? QStringList{ qEnvironmentVariable( "SICNU_TRANSLATIONS_DIR" ),
                                      application.applicationDirPath() + QStringLiteral( "/resources/translations" ),
                                      application.applicationDirPath() + QStringLiteral( "/../resources/translations" ),
                                      application.applicationDirPath() + QStringLiteral( "/translations" ) }
                                  : directories;
  auto appTranslator = std::make_unique<QTranslator>();
  auto qtTranslator = std::make_unique<QTranslator>();
  const auto load = [&]( QTranslator &translator, const QString &catalog ) {
    for ( const QString &directory : candidates )
      if ( !directory.isEmpty() && translator.load( catalog, directory ) )
        return true;
    return false;
  };
  // Keep both catalogs together: application labels and standard Qt buttons
  // must not silently end up in different languages in an offline install.
  if ( !load( *appTranslator, QStringLiteral( "sicnu_zh_CN" ) )
       || !load( *qtTranslator, QStringLiteral( "qtbase_zh_CN" ) ) )
  {
    qWarning() << "i18n: application or Qt catalog missing; using English";
    return QStringLiteral( "en" );
  }
  qtTranslator->setObjectName( QStringLiteral( "sicnuQtTranslator" ) );
  appTranslator->setObjectName( QStringLiteral( "sicnuAppTranslator" ) );
  qtTranslator->setParent( &application );
  appTranslator->setParent( &application );
  application.installTranslator( qtTranslator.release() );
  application.installTranslator( appTranslator.release() );
  return language;
}

} // namespace SicnuUi
