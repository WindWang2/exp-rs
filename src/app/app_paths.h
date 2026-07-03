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
   * Checks project-root samples_data/, data/samples_data/, and install paths.
   */
  static QString samplesDataDir()
  {
    const QStringList candidates = {
      resolveDataPath( "samples_data" ),
      resolveDataPath( "data/samples_data" ),
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
};
