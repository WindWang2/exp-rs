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

namespace
{
/// R32-T06: drop catalogs installed by an earlier call in this process.
///
/// Switching zh_CN -> en in one process used to take the "en" fast path and
/// leave the Chinese catalogs installed, while effectiveUiLanguage() reported
/// "en" — so the chrome stayed Chinese and the guide resolved to English, the
/// exact mixed-language state the single-authority helper exists to prevent.
/// The same applies when a catalog is missing and we fall back to English.
void removeInstalledTranslators( QCoreApplication &application )
{
  const QList<QTranslator *> installed = application.findChildren<QTranslator *>();
  for ( QTranslator *translator : installed )
  {
    const QString name = translator->objectName();
    if ( name != QLatin1String( "sicnuAppTranslator" )
         && name != QLatin1String( "sicnuQtTranslator" ) )
      continue;
    application.removeTranslator( translator );
    translator->setParent( nullptr );
    delete translator;
  }
}
} // namespace

QString installUiTranslations( QCoreApplication &application, const QStringList &directories )
{
  // Use the PURE preference resolution here, not installedUiLanguage(): a
  // previous call's fallback must not poison a later re-install in the same
  // process (isolated package validation calls this repeatedly).
  const QString language = currentUiLanguage();
  // Every branch below installs either both catalogs or none, so the previous
  // install must go first regardless of which branch runs.
  removeInstalledTranslators( application );
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
