// src/agent/raster_display_service.h
#pragma once

#include <json/json.h>
#include <QObject>
#include <QString>
#include <QThread>
#include <memory>
#include <optional>
#include <utility>

class QgsMapCanvas;
class QgsRasterLayer;

namespace sicnu::data {
class DataManager;
}

namespace sicnu::display {
class QgisDisplayManager;
}

namespace sicnu::agent {

/**
 * @brief Unified service for managing raster layer display properties.
 *
 * Responsibilities:
 * - Raster renderer management (MultiBandColor, SingleBandGray)
 * - Band composition resolution (including semantic BandRoles such as red, green, blue, nir, swir1, swir2)
 * - Contrast enhancement and stretch settings (minimum_maximum, percent_clip, stddev)
 * - Layer opacity
 * - Display revision tracking for Agent freshness checks
 *
 * All operations are thread-safe and marshaled to the GUI thread.
 */
class RasterDisplayService : public QObject {
  Q_OBJECT

public:
  explicit RasterDisplayService( QObject *parent = nullptr );
  RasterDisplayService( sicnu::display::QgisDisplayManager *displayManager,
                        QgsMapCanvas *canvas,
                        sicnu::data::DataManager *dataManager = nullptr,
                        QObject *parent = nullptr );
  ~RasterDisplayService() override = default;

  void setDisplayManager( sicnu::display::QgisDisplayManager *dm );
  sicnu::display::QgisDisplayManager *displayManager() const { return m_displayManager; }

  void setMapCanvas( QgsMapCanvas *canvas );
  QgsMapCanvas *mapCanvas() const { return m_canvas; }

  void setDataManager( sicnu::data::DataManager *dm );
  sicnu::data::DataManager *dataManager() const { return m_dataManager; }

  void setActiveLayerName( const QString &name );
  QString activeLayerName() const;

  quint64 displayRevision() const { return m_displayRevision; }

  // Tool: raster:get_display
  // Returns: { "status": "success", "renderer": "...", "bands": {...}, "stretch": {...}, "opacity": 1.0, "displayRevision": N }
  Json::Value getDisplay( const Json::Value &params = Json::Value() );

  // Tool: raster:set_band_composite
  // Parameters: { "layer": "...", "red": "...", "green": "...", "blue": "...", "opacity": 1.0 }
  Json::Value setBandComposite( const Json::Value &params );

  // Tool: raster:set_stretch
  // Parameters: { "layer": "...", "method": "percent_clip"|"minimum_maximum"|"stddev", "lower": 2, "upper": 98, "factor": 2.0, ... }
  Json::Value setStretch( const Json::Value &params );

  // Tool: raster:reset_display
  // Parameters: { "layer": "..." }
  Json::Value resetDisplay( const Json::Value &params = Json::Value() );

  // Helper method for resolving band role or band index on a raster layer:
  // Returns pair of <1-based band index, error message>. If successful, error message is empty.
  std::pair<int, QString> resolveBand( QgsRasterLayer *layer, const Json::Value &bandVal ) const;

  // Helper to find a raster layer by ID / name / active layer
  QgsRasterLayer *findRasterLayer( const QString &identifier, QString *errorOut = nullptr ) const;

signals:
  void displayChanged( quint64 revision );

private:
  template <typename Func>
  auto executeOnGuiThread( Func &&func ) const -> decltype( func() )
  {
    using ReturnType = decltype( func() );
    if ( QThread::currentThread() == this->thread() || !this->thread() )
    {
      return func();
    }
    ReturnType result;
    QMetaObject::invokeMethod(
      const_cast<RasterDisplayService *>( this ),
      [&]() {
        result = func();
      },
      Qt::BlockingQueuedConnection );
    return result;
  }

  void incrementRevision();

  sicnu::display::QgisDisplayManager *m_displayManager = nullptr;
  QgsMapCanvas *m_canvas = nullptr;
  sicnu::data::DataManager *m_dataManager = nullptr;
  QString m_activeLayerName;
  quint64 m_displayRevision = 1;
};

} // namespace sicnu::agent
