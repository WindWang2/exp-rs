#pragma once

#include <QString>
#include <QList>
#include <optional>

/**
 * A preset definition for a commonly used Coordinate Reference System.
 */
struct CrsPreset
{
  QString name;
  int epsgCode;
  QString description;
  QString category;
};

/**
 * Provides a catalog of commonly used CRS presets organized by category.
 *
 * Categories: "Global", "UTM", "China", "Regional"
 */
namespace CrsPresets
{
  /**
   * Returns all CRS presets.
   */
  QList<CrsPreset> allPresets();

  /**
   * Returns CRS presets filtered by category name.
   */
  QList<CrsPreset> presetsByCategory( const QString &category );

  /**
   * Returns the list of available category names.
   */
  QStringList categories();

  /**
   * Finds a preset by EPSG code.
   * Returns std::nullopt if no matching preset is found.
   */
  std::optional<CrsPreset> presetForEpsg( int epsg );

  /**
   * Records a CRS as recently used. Stores in QSettings.
   * Moves to front if already present; limits to 10 entries.
   */
  void addRecentCrs( int epsg );

  /**
   * Returns recently used CRS presets (most recent first).
   */
  QList<CrsPreset> recentPresets();
}
