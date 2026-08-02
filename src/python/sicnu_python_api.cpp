// sicnu_python_api.cpp — Python API bindings for SICNU GEO RS platform
#include "sicnu_python_api.h"

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsvectordataprovider.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsrectangle.h>
#include <qgsgeometry.h>
#include <qgsfeature.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>

#include "active_view_host.h"

#include <QFileInfo>
#include <QCoreApplication>

SicnuPythonApi &SicnuPythonApi::instance()
{
    static SicnuPythonApi s_instance;
    return s_instance;
}

SicnuPythonApi::SicnuPythonApi(QObject *parent)
    : QObject(parent)
{
}

// ---- Project ----

QString SicnuPythonApi::projectPath() const
{
    return QgsProject::instance()->fileName();
}

bool SicnuPythonApi::saveProject()
{
    return QgsProject::instance()->write();
}

bool SicnuPythonApi::openProject(const QString &path)
{
    return QgsProject::instance()->read(path);
}

// ---- Layers ----

QStringList SicnuPythonApi::layerNames() const
{
    QStringList names;
    const auto layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        names.append(it.value()->name());
    }
    return names;
}

int SicnuPythonApi::layerCount() const
{
    return QgsProject::instance()->mapLayers().size();
}

QString SicnuPythonApi::addRasterLayer(const QString &path, const QString &name)
{
    Q_UNUSED(name);
    // Data/Display seam (ADR 0009/0010/0015/0043): ActiveViewHost only.
    if (!m_activeViewHost || path.isEmpty())
        return QString();
    const auto res = m_activeViewHost->openRasterPath(path);
    if (!res)
        return QString();
    return QFileInfo(path).baseName();
}

QString SicnuPythonApi::addVectorLayer(const QString &path, const QString &name)
{
    Q_UNUSED(name);
    // Data/Display seam (ADR 0009/0010/0015/0043): ActiveViewHost only.
    if (!m_activeViewHost || path.isEmpty())
        return QString();
    const auto res = m_activeViewHost->openVectorPath(path);
    if (!res)
        return QString();
    return QFileInfo(path).baseName();
}

bool SicnuPythonApi::removeLayer(const QString &layerName)
{
    // Data/Display seam (ADR 0043): route through QgsProject layer lookup +
    // ActiveViewHost refresh. The display manager owns layer presentation;
    // QgsProject::removeMapLayer handles the legacy/external path.
    const auto layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        if (it.value()->name() == layerName) {
            QgsProject::instance()->removeMapLayer(it.key());
            if (m_activeViewHost) m_activeViewHost->refreshCanvas();
            return true;
        }
    }
    return false;
}

// ---- Raster Operations ----

QVariantMap SicnuPythonApi::rasterInfo(const QString &layerName) const
{
    QVariantMap info;
    const auto layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        auto *rl = qobject_cast<QgsRasterLayer *>(it.value());
        if (rl && rl->name() == layerName) {
            info["name"] = rl->name();
            info["path"] = rl->source();
            info["width"] = rl->width();
            info["height"] = rl->height();
            info["bandCount"] = rl->bandCount();
            info["crs"] = rl->crs().authid();
            info["extent"] = QString("%1,%2,%3,%4")
                .arg(rl->extent().xMinimum())
                .arg(rl->extent().yMinimum())
                .arg(rl->extent().xMaximum())
                .arg(rl->extent().yMaximum());
            return info;
        }
    }
    return info;
}

QList<double> SicnuPythonApi::pixelValue(const QString &layerName, double x, double y) const
{
    QList<double> values;
    const auto layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        auto *rl = qobject_cast<QgsRasterLayer *>(it.value());
        if (rl && rl->name() == layerName && rl->dataProvider()) {
            QgsPointXY point(x, y);
            for (int band = 1; band <= rl->bandCount(); ++band) {
                bool ok;
                double val = rl->dataProvider()->sample(point, band, &ok);
                values.append(ok ? val : std::numeric_limits<double>::quiet_NaN());
            }
            return values;
        }
    }
    return values;
}

QVariantMap SicnuPythonApi::bandStatistics(const QString &layerName, int band) const
{
    QVariantMap stats;
    const auto layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        auto *rl = qobject_cast<QgsRasterLayer *>(it.value());
        if (rl && rl->name() == layerName && rl->dataProvider()) {
            QgsRasterBandStats bandStats = rl->dataProvider()->bandStatistics(band);
            stats["min"] = bandStats.minimumValue;
            stats["max"] = bandStats.maximumValue;
            stats["mean"] = bandStats.mean;
            stats["stdDev"] = bandStats.stdDev;
            stats["range"] = bandStats.range;
            return stats;
        }
    }
    return stats;
}

// ---- Vector Operations ----

QVariantMap SicnuPythonApi::vectorInfo(const QString &layerName) const
{
    QVariantMap info;
    const auto layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        auto *vl = qobject_cast<QgsVectorLayer *>(it.value());
        if (vl && vl->name() == layerName) {
            info["name"] = vl->name();
            info["path"] = vl->source();
            info["featureCount"] = vl->featureCount();
            info["geometryType"] = static_cast<int>(vl->geometryType());
            info["crs"] = vl->crs().authid();
            info["fields"] = vl->fields().names();
            return info;
        }
    }
    return info;
}

int SicnuPythonApi::featureCount(const QString &layerName) const
{
    const auto layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        auto *vl = qobject_cast<QgsVectorLayer *>(it.value());
        if (vl && vl->name() == layerName) {
            return vl->featureCount();
        }
    }
    return 0;
}

// ---- Processing ----

bool SicnuPythonApi::runAlgorithm(const QString &algorithmId, const QVariantMap &parameters)
{
    const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById(algorithmId);
    if (!alg) return false;

    std::unique_ptr<QgsProcessingAlgorithm> algo(alg->create());
    QgsProcessingContext context;
    context.setProject(QgsProject::instance());
    QgsProcessingFeedback feedback;

    bool ok = false;
    algo->run(parameters, context, &feedback, &ok);
    return ok;
}

// ---- Map Canvas (routed through ActiveViewHost, ADR 0043) ----

QVariantMap SicnuPythonApi::canvasExtent() const
{
    QVariantMap extent;
    if (m_activeViewHost) {
        QgsRectangle ext = m_activeViewHost->mapCanvasExtent();
        extent["xmin"] = ext.xMinimum();
        extent["ymin"] = ext.yMinimum();
        extent["xmax"] = ext.xMaximum();
        extent["ymax"] = ext.yMaximum();
    }
    return extent;
}

void SicnuPythonApi::setCanvasExtent(double xmin, double ymin, double xmax, double ymax)
{
    if (m_activeViewHost) {
        m_activeViewHost->setExtent(QgsRectangle(xmin, ymin, xmax, ymax));
    }
}

void SicnuPythonApi::refreshCanvas()
{
    if (m_activeViewHost) m_activeViewHost->refreshCanvas();
}

double SicnuPythonApi::canvasScale() const
{
    return m_activeViewHost ? m_activeViewHost->mapCanvasScale() : 0.0;
}

void SicnuPythonApi::setCanvasScale(double scale)
{
    if (m_activeViewHost) {
        m_activeViewHost->setScale(scale);
    }
}

// ---- CRS (routed through ActiveViewHost, ADR 0043) ----

QString SicnuPythonApi::projectCrs() const
{
    return QgsProject::instance()->crs().authid();
}

bool SicnuPythonApi::setProjectCrs(const QString &crsString)
{
    QgsCoordinateReferenceSystem crs(crsString);
    if (!crs.isValid()) return false;
    QgsProject::instance()->setCrs(crs);
    if (m_activeViewHost) m_activeViewHost->refreshCanvas();
    return true;
}

// ---- Application ----

QString SicnuPythonApi::applicationPath() const
{
    return QCoreApplication::applicationDirPath();
}

QString SicnuPythonApi::version() const
{
    return QStringLiteral("SICNU GEO RS v0.9.2-dev");
}
