// src/python/isolated/app_interface_bridge.h
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "data/asset_types.h"
#include "data/data_result.h"

class ActiveViewHost;
class QgsMapLayer;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::python::isolated
{

struct ActiveLayerSummary
{
  bool isValid = false;
  QString name;
  QString source;
  QString type; // "raster" or "vector"
  QString crs;

  QJsonObject toJsonObject() const;
};

struct CanvasViewportSummary
{
  bool isValid = false;
  double xMin = 0.0;
  double yMin = 0.0;
  double xMax = 0.0;
  double yMax = 0.0;
  double scale = 1.0;

  QJsonObject toJsonObject() const;
};

/**
 * AppInterfaceBridge — JSON-RPC serialization bridge module.
 *
 * Headless asset seam: `DataManager` is the required asset authority
 * (catalog queries, source registration, active-asset tracking), while
 * `ActiveViewHost` is an optional display/canvas/message-bar enhancement
 * bound only in GUI mode. Serves as the dedicated JSON-RPC IPC presentation
 * & state serialization layer consumed by PythonAppInterfaceProxy for
 * out-of-process Python plugin workers (ADR 0014/0015).
 */
class AppInterfaceBridge : public QObject
{
  Q_OBJECT

  public:
    explicit AppInterfaceBridge( sicnu::data::DataManager *dataManager = nullptr,
                                 ActiveViewHost *activeViewHost = nullptr,
                                 QObject *parent = nullptr );
    ~AppInterfaceBridge() override = default;

    void setDataManager( sicnu::data::DataManager *dataManager );
    sicnu::data::DataManager *dataManager() const;

    void setActiveViewHost( ActiveViewHost *host );
    ActiveViewHost *activeViewHost() const;

    ActiveLayerSummary getActiveLayerSummary() const;
    QgsMapLayer *activeLayer() const;

    bool openPath( const QString &path );

    /// Plugin-driven "active layer": validates the asset exists in the
    /// catalog, then makes it the active asset. Returns false otherwise.
    bool setActiveAsset( const sicnu::data::AssetId &assetId );
    sicnu::data::AssetId activeAssetId() const;

    CanvasViewportSummary getCanvasViewportSummary() const;

    bool pushMessageBarAlert( const QString &title, const QString &text, int level = 0 );

  private:
    sicnu::data::DataManager *m_dataManager = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
    sicnu::data::AssetId m_activeAssetId;
};

} // namespace sicnu::python::isolated
