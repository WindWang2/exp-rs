// src/python/isolated/app_interface_bridge.h
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

#include "data/asset_types.h"
#include "data/data_result.h"

class ActiveViewHost;
class QgsMapLayer;

namespace sicnu::data
{
class DataManager;
}

#include <QAction>
#include <QMenu>
#include <QMap>

namespace sicnu::python::isolated
{

class PythonIpcServer;

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
 * AppInterfaceBridge — JSON-RPC IPC facade & state serialization layer (ADR 0025, ADR 0035).
 *
 * Headless asset seam: DataManager is the required asset authority (catalog queries,
 * source registration, active-asset tracking), while ActiveViewHost and QMenu are
 * optional display/canvas/menu enhancements bound when in GUI mode.
 */
class AppInterfaceBridge : public QObject
{
  Q_OBJECT

  public:
    explicit AppInterfaceBridge( sicnu::data::DataManager *dataManager = nullptr,
                                 ActiveViewHost *activeViewHost = nullptr,
                                 QMenu *parentMenu = nullptr,
                                 QObject *parent = nullptr );
    ~AppInterfaceBridge() override = default;

    void setDataManager( sicnu::data::DataManager *dataManager );
    sicnu::data::DataManager *dataManager() const;

    void setActiveViewHost( ActiveViewHost *host );
    ActiveViewHost *activeViewHost() const;

    void setParentMenu( QMenu *parentMenu );
    QMenu *parentMenu() const;

    void bindIpcServer( PythonIpcServer *ipcServer );
    PythonIpcServer *ipcServer() const;

    int registeredActionCount() const;

    ActiveLayerSummary getActiveLayerSummary() const;
    QgsMapLayer *activeLayer() const;

    bool openPath( const QString &path );

    /// Plugin-driven "active layer": validates the asset exists in the
    /// catalog, then makes it the active asset. Returns false otherwise.
    bool setActiveAsset( const sicnu::data::AssetId &assetId );
    sicnu::data::AssetId activeAssetId() const;

    CanvasViewportSummary getCanvasViewportSummary() const;

    bool pushMessageBarAlert( const QString &title, const QString &text, int level = 0 );

    using AlgorithmRegisterHandler = std::function<bool( const QString &algoId,
                                                          const QString &name,
                                                          const QString &group,
                                                          const QString &desc )>;
    void setAlgorithmRegisterHandler( AlgorithmRegisterHandler handler )
    {
      m_algoRegisterHandler = std::move( handler );
    }

    /// Deep JSON-RPC IPC method dispatch seam (ADR 0025).
    /// Decodes request, executes domain action, populates response JSON, and
    /// returns true if handled headlessly.
    bool dispatchIpcMessage( const QJsonObject &message, QJsonObject &response );

  public slots:
    void handleIpcMessage( const QJsonObject &message );

  signals:
    void actionTriggered( const QString &callbackId );

  private:
    void setupDefaultAlgorithmHandler();

    sicnu::data::DataManager *m_dataManager = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
    QMenu *m_parentMenu = nullptr;
    PythonIpcServer *m_ipcServer = nullptr;
    sicnu::data::AssetId m_activeAssetId;
    AlgorithmRegisterHandler m_algoRegisterHandler;
    QMap<QString, QAction *> m_registeredActions;
};

} // namespace sicnu::python::isolated
