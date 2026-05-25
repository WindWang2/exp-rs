#include <pybind11/pybind11.h>
#include "antigravity_init.h"
#include <qgsrasterlayer.h>
#include <qgsmapsettings.h>
#include <qgsmaprenderersequentialjob.h>
#include <qgscoordinatetransform.h>
#include <QImage>

namespace py = pybind11;

static py::dict open_raster(const std::string &path) {
    QgsRasterLayer layer(QString::fromStdString(path), "r", "gdal");
    py::dict d;
    d["valid"] = layer.isValid();
    if (layer.isValid()) {
        d["width"] = layer.width();
        d["height"] = layer.height();
        d["crs"] = layer.crs().authid().toStdString();
    }
    return d;
}

static bool render_to_png(const std::string &path, const std::string &out, int size) {
    QgsRasterLayer *layer = new QgsRasterLayer(QString::fromStdString(path), "r", "gdal");
    if (!layer->isValid()) { delete layer; return false; }
    QgsMapSettings s;
    s.setLayers({layer});
    s.setExtent(layer->extent());
    s.setOutputSize(QSize(size, size));
    s.setDestinationCrs(layer->crs());
    QgsMapRendererSequentialJob job(s);
    job.start();
    job.waitForFinished();
    QImage img = job.renderedImage();
    delete layer;
    return !img.isNull() && img.save(QString::fromStdString(out));
}

PYBIND11_MODULE(_antigravity_core, m) {
    m.def("init", [](const std::string &root) {
        antigravity_init(QString::fromStdString(root));
        // Clear the coordinate-transform cache via Python atexit so that the
        // global sTransforms hash is empty when libqgis_core.so's DSO destructors
        // run, preventing QgsCoordinateTransformPrivate::freeProj bad_alloc.
        py::module_::import("atexit").attr("register")(
            py::cpp_function([]() {
                QgsCoordinateTransform::invalidateCache(true);
            })
        );
    });
    m.def("open_raster", &open_raster);
    m.def("render_to_png", &render_to_png);
}
