#include "application_language.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

#include <memory>

namespace SicnuUi
{

QString installUiTranslations( QCoreApplication &application, const QStringList &directories )
{
  // Use the PURE preference resolution here, not installedUiLanguage(): a
  // previous call's fallback must not poison a later re-install in the same
  // process (isolated package validation calls this repeatedly).
  const QString language = currentUiLanguage();
  if ( language == QLatin1String( "en" ) )
  {
    setInstalledUiLanguage( language );
    return language;
  }

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
    setInstalledUiLanguage( QStringLiteral( "en" ) );
    return QStringLiteral( "en" );
  }
  qtTranslator->setObjectName( QStringLiteral( "sicnuQtTranslator" ) );
  appTranslator->setObjectName( QStringLiteral( "sicnuAppTranslator" ) );
  qtTranslator->setParent( &application );
  appTranslator->setParent( &application );
  application.installTranslator( qtTranslator.release() );
  application.installTranslator( appTranslator.release() );
  setInstalledUiLanguage( language );
  return language;
}

} // namespace SicnuUi
