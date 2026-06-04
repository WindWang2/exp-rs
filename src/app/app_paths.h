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
};
