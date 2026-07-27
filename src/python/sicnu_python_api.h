// sicnu_python_api.h — Python API bindings for SICNU GEO RS platform
#pragma once

#include <QString>
#include <QObject>
#include <QVariantMap>

class QgsMapCanvas;
class QgsProject;
class QgsRasterLayer;
class QgsVectorLayer;

class ActiveViewHost;

/**
 * Provides Python-accessible API to the SICNU GEO RS platform.
 * This class exposes core functionality that Python scripts can use.
 */
class SicnuPythonApi : public QObject
{
    Q_OBJECT

public:
    static SicnuPythonApi &instance();

    void initialize(QgsMapCanvas *canvas);
    void setActiveViewHost(ActiveViewHost *host) { m_activeViewHost = host; }
    ActiveViewHost *activeViewHost() const { return m_activeViewHost; }

    // ---- Project ----
    Q_INVOKABLE QString projectPath() const;
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool openProject(const QString &path);

    // ---- Layers ----
    Q_INVOKABLE QStringList layerNames() const;
    Q_INVOKABLE int layerCount() const;
    Q_INVOKABLE QString addRasterLayer(const QString &path, const QString &name = QString());
    Q_INVOKABLE QString addVectorLayer(const QString &path, const QString &name = QString());
    Q_INVOKABLE bool removeLayer(const QString &layerName);

    // ---- Raster Operations ----
    Q_INVOKABLE QVariantMap rasterInfo(const QString &layerName) const;
    Q_INVOKABLE QList<double> pixelValue(const QString &layerName, double x, double y) const;
    Q_INVOKABLE QVariantMap bandStatistics(const QString &layerName, int band) const;

    // ---- Vector Operations ----
    Q_INVOKABLE QVariantMap vectorInfo(const QString &layerName) const;
    Q_INVOKABLE int featureCount(const QString &layerName) const;

    // ---- Processing ----
    Q_INVOKABLE bool runAlgorithm(const QString &algorithmId, const QVariantMap &parameters);

    // ---- Map Canvas ----
    Q_INVOKABLE QVariantMap canvasExtent() const;
    Q_INVOKABLE void setCanvasExtent(double xmin, double ymin, double xmax, double ymax);
    Q_INVOKABLE void refreshCanvas();
    Q_INVOKABLE double canvasScale() const;
    Q_INVOKABLE void setCanvasScale(double scale);

    // ---- CRS ----
    Q_INVOKABLE QString projectCrs() const;
    Q_INVOKABLE bool setProjectCrs(const QString &crsString);

    // ---- Application ----
    Q_INVOKABLE QString applicationPath() const;
    Q_INVOKABLE QString version() const;

private:
    SicnuPythonApi(QObject *parent = nullptr);

    QgsMapCanvas *m_canvas = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
};
