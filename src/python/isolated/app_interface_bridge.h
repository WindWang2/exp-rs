// src/python/isolated/app_interface_bridge.h
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "data/data_result.h"

class ActiveViewHost;
class QgsMapLayer;

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
 * Consolidates Python QGIS method routing (activeLayer, openPath, canvasState, pushMessageBarAlert)
 * to ActiveViewHost. Serves as the dedicated JSON-RPC IPC presentation & state serialization layer
 * consumed by PythonAppInterfaceProxy for out-of-process Python plugin workers (ADR 0014).
 */
class AppInterfaceBridge : public QObject
{
  Q_OBJECT

  public:
    explicit AppInterfaceBridge( ActiveViewHost *activeViewHost = nullptr, QObject *parent = nullptr );
    ~AppInterfaceBridge() override = default;

    void setActiveViewHost( ActiveViewHost *host );
    ActiveViewHost *activeViewHost() const;

    ActiveLayerSummary getActiveLayerSummary() const;
    QgsMapLayer *activeLayer() const;

    bool openPath( const QString &path );

    CanvasViewportSummary getCanvasViewportSummary() const;

    bool pushMessageBarAlert( const QString &title, const QString &text, int level = 0 );

  private:
    ActiveViewHost *m_activeViewHost = nullptr;
};

} // namespace sicnu::python::isolated
