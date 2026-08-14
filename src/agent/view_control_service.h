// src/agent/view_control_service.h
#pragma once

#include <json/json.h>
#include <QObject>
#include <QString>
#include <QThread>
#include <memory>
#include <optional>

class QgsMapCanvas;
class QgsRubberBand;
class QgsMapLayer;

namespace sicnu::data {
class DataManager;
}

namespace sicnu::display {
class QgisDisplayManager;
}

namespace sicnu::agent {

/**
 * @brief GUI thread safe service for observing and controlling map view, layers, and canvas state.
 *
 * Provides view state queries (view ID, CRS, extent, scale, rotation, active layer)
 * and view controls (set_extent, zoom_to_layer, zoom_to_asset, fit_all, set_scale, roi:set, roi:clear).
 * All public methods return structured JSON and safely marshal calls to the GUI thread.
 */
class ViewControlService : public QObject {
  Q_OBJECT

public:
  explicit ViewControlService( QObject *parent = nullptr );
  ViewControlService( sicnu::display::QgisDisplayManager *displayManager,
                      QgsMapCanvas *canvas,
                      sicnu::data::DataManager *dataManager = nullptr,
                      QObject *parent = nullptr );
  ~ViewControlService() override;

  void setDisplayManager( sicnu::display::QgisDisplayManager *dm );
  sicnu::display::QgisDisplayManager *displayManager() const { return m_displayManager; }

  void setMapCanvas( QgsMapCanvas *canvas );
  QgsMapCanvas *mapCanvas() const { return m_canvas; }

  void setDataManager( sicnu::data::DataManager *dm );
  sicnu::data::DataManager *dataManager() const { return m_dataManager; }

  void setActiveLayerName( const QString &name );
  QString activeLayerName() const;

  QString lastRoiWkt() const;
  QString lastRoiCrs() const;

  // View state query
  Json::Value getState( const Json::Value &params = Json::Value() );

  // View navigation and extent controls
  Json::Value setExtent( const Json::Value &params );
  Json::Value zoomToLayer( const Json::Value &params );
  Json::Value zoomToAsset( const Json::Value &params );
  Json::Value fitAll( const Json::Value &params = Json::Value() );
  Json::Value setScale( const Json::Value &params );

  // Canvas Region of Interest (ROI) controls
  Json::Value setRoi( const Json::Value &params );
  Json::Value clearRoi( const Json::Value &params = Json::Value() );

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
      const_cast<ViewControlService *>( this ),
      [&]() {
        result = func();
      },
      Qt::BlockingQueuedConnection );
    return result;
  }

  QgsMapLayer *findLayer( const QString &identifier ) const;

  sicnu::display::QgisDisplayManager *m_displayManager = nullptr;
  QgsMapCanvas *m_canvas = nullptr;
  sicnu::data::DataManager *m_dataManager = nullptr;
  QString m_activeLayerName;

  QgsRubberBand *m_roiRubberBand = nullptr;
  QString m_lastRoiWkt;
  QString m_lastRoiCrs;
};

} // namespace sicnu::agent
