#pragma once
#include <QString>
#include <QStringList>

/**
 * Central help text for Processing toolbox algorithms.
 * Used by GDAL/OTB wrappers and Generic CLI for tooltips & dialog help.
 */
namespace SicnuAlgorithmHelp
{
  /** One-line description for toolbox hover (shortDescription). */
  QString shortDescription( const QString &algorithmName, const QString &displayName = QString() );

  /** Multi-paragraph HTML/plain help for algorithm dialog (shortHelpString). */
  QString shortHelpString( const QString &algorithmName, const QString &displayName = QString(),
                           const QString &cliName = QString(), const QStringList &tags = {} );

  /** Every catalog key (the algorithm names the catalog can answer for).
   *  Drift gate input: each key must match a registered algorithm's name()
   *  — a key no provider answers is unreachable help rotting in place
   *  (#1646-5's regression guard). Sorted for stable assertions. */
  QStringList catalogKeys();
}
