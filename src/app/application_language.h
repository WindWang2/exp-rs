#pragma once

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QString>
#include <QStringList>

class QCoreApplication;

namespace SicnuUi
{

/// Resolve the saved choice and optional launch override to a supported UI language.
/// Pure and inline so any first-party UI module (dialogs, panels, help) can share
/// this single authority without adding a link dependency.
inline QString resolveUiLanguage( const QString &preference, const QString &launchOverride,
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

/// Effective UI language for the current process. Uses exactly the same inputs
/// as installUiTranslations(): the saved QSettings preference, the SICNU_LANG
/// launch override, then the system UI language. Restart-effective, matching
/// #2064/#2065; no second preference store and no live language switch.
inline QString currentUiLanguage()
{
  const QLocale systemLocale = QLocale::system();
  const QString systemLanguage = systemLocale.uiLanguages().value( 0, systemLocale.name() );
  return resolveUiLanguage(
    QSettings().value( QStringLiteral( "preferences/uiLanguage" ), QStringLiteral( "system" ) ).toString(),
    qEnvironmentVariable( "SICNU_LANG" ), systemLanguage );
}

namespace detail
{
/// Language ACTUALLY installed by the last installUiTranslations() call.
/// Empty when bootstrap never ran (help-only tests, CLI tools). Kept separate
/// from currentUiLanguage() so #2064/#2065 preference semantics stay intact.
inline QString &installedLanguageStorage()
{
  static QString language;
  return language;
}
} // namespace detail

/// Records the language really in effect after catalog loading. A missing
/// app/Qt catalog forces English even when the saved preference is zh_CN
/// (#2064/#2065); consumers that must agree with the visible UI (manual/help
/// resolution) read installedUiLanguage() instead of the raw preference.
inline void setInstalledUiLanguage( const QString &language )
{
  detail::installedLanguageStorage() = language;
}

inline QString installedUiLanguage()
{
  return detail::installedLanguageStorage();
}

/// The language the VISIBLE UI is actually in. Bootstrap records the language
/// really installed ("en" when a catalog is missing); before bootstrap falls
/// back to the saved preference. Every localized surface (manual resolution,
/// panel action labels, status text) must read THIS, not the raw preference,
/// so a degraded install never shows mixed-language UI (R3 P2-3).
inline QString effectiveUiLanguage()
{
  const QString installed = installedUiLanguage();
  return installed.isEmpty() ? currentUiLanguage() : installed;
}

/// Install application and Qt catalogs before constructing any UI. A missing
/// catalog falls back to English for both. Explicit directories support
/// isolated package validation; the default searches beside the executable.
QString installUiTranslations( QCoreApplication &application,
                               const QStringList &directories = {} );

} // namespace SicnuUi
