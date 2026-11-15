#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

namespace SicnuUi
{

/// Resolve the saved choice and optional launch override to a supported UI language.
QString resolveUiLanguage( const QString &preference, const QString &launchOverride,
                           const QString &systemLanguage );

/// Install application and Qt catalogs before constructing any UI. A missing
/// catalog falls back to English for both. Explicit directories support
/// isolated package validation; the default searches beside the executable.
QString installUiTranslations( QCoreApplication &application,
                               const QStringList &directories = {} );

} // namespace SicnuUi
