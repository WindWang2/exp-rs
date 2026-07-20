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
}
