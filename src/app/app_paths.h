#pragma once

#include "processing/framework/runtime_paths.h"

#include <QString>
#include <QDir>
#include <QCoreApplication>

/**
 * Utility class for resolving application paths dynamically.
 * Replaces all hardcoded paths with runtime-resolved equivalents.
 *
 * The project-root path resolution is owned by
 * sicnu::processing::resolveRuntimeDataPath (a pure helper shared with the
 * processing layer so it does not have to reach up into this app header — an
 * inverted dependency, perf/architecture goal §3b). AppPaths keeps its
 * app-facing facade and delegates.
 */
class AppPaths
{
public:
  /**
   * Returns the application's prefix path (for QgsApplication::setPrefixPath).
   * Uses the executable's location to find the install prefix.
   */
  static QString prefixPath()
  {
    QDir dir( QCoreApplication::applicationDirPath() );
    if ( dir.dirName() == QLatin1String( "bin" ) )
      dir.cdUp();
    return dir.absolutePath();
  }

  /**
   * Resolves a data file path relative to the project root.
   * Delegates to the shared runtime path helper (single owner of the logic).
   */
  static QString resolveDataPath( const QString &relativePath )
  {
    return sicnu::processing::resolveRuntimeDataPath( relativePath );
  }

  /**
   * Returns the path to the sample data directory.
   */
  static QString dataDir()
  {
    return resolveDataPath( "data" );
  }

  /**
   * Returns the bundled lab sample datasets directory (rasters, vectors).
   * Prefers data/samples/; keeps legacy samples_data/ candidates for one release.
   */
  static QString samplesDataDir()
  {
    const QStringList candidates = {
      resolveDataPath( "data/samples" ),
      resolveDataPath( "samples_data" ),
      resolveDataPath( "data/samples_data" ),
      QDir( QCoreApplication::applicationDirPath() ).absoluteFilePath( "../data/samples" ),
      QDir( QCoreApplication::applicationDirPath() ).absoluteFilePath( "data/samples" ),
      QDir( QCoreApplication::applicationDirPath() ).absoluteFilePath( "../samples_data" ),
      QDir( QCoreApplication::applicationDirPath() ).absoluteFilePath( "samples_data" ),
    };

    for ( const QString &candidate : candidates )
    {
      if ( QDir( candidate ).exists() )
        return QDir( candidate ).absolutePath();
    }

    return {};
  }

  /**
   * QGIS reference resources (symbology, etc.).
   * Source tree: refs/qgis/; install layout may still use qgis_ref/.
   */
  static QString qgisRefResourcesDir()
  {
    const QStringList candidates = {
      resolveDataPath( "refs/qgis/resources" ),
      resolveDataPath( "qgis_ref/resources" ),
      QDir( QCoreApplication::applicationDirPath() ).absoluteFilePath( "../share/sicnu_geo_rs/qgis_ref/resources" ),
      QDir( QCoreApplication::applicationDirPath() ).absoluteFilePath( "share/sicnu_geo_rs/qgis_ref/resources" ),
    };

    for ( const QString &candidate : candidates )
    {
      if ( QDir( candidate ).exists() )
        return QDir( candidate ).absolutePath();
    }

    return {};
  }
};
