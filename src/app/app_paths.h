#pragma once

#include <QString>
#include <QDir>
#include <QCoreApplication>

/**
 * Utility class for resolving application paths dynamically.
 * Replaces all hardcoded paths with runtime-resolved equivalents.
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
    return QCoreApplication::applicationDirPath();
  }

  /**
   * Resolves a data file path relative to the project root.
   * Navigates up from the executable directory to find the project root.
   */
  static QString resolveDataPath( const QString &relativePath )
  {
    QDir dir( QCoreApplication::applicationDirPath() );
    // From build-tests/tests/ -> build-tests/ -> project root
    // From build/ -> project root
    // From install/bin/ -> install/
    while ( !dir.exists( "CMakeLists.txt" ) && !dir.exists( "data" ) && dir.cdUp() )
    {
      // keep going up
    }
    return dir.absoluteFilePath( relativePath );
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
