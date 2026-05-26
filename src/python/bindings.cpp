#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "qt_casters.h"
#include "antigravity_init.h"

// Core rendering (P0)
#include <qgsrasterlayer.h>
#include <qgsmapsettings.h>
#include <qgsmaprenderersequentialjob.h>
#include <qgscoordinatetransform.h>
#include <QImage>

// P1: geometry + feature
#include <qgsgeometry.h>
#include <qgsfeature.h>
#include <qgsfield.h>
#include <qgsfields.h>
#include <qgswkbtypes.h>

// P1: layers
#include <qgsmaplayer.h>
#include <qgsvectorlayer.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsrectangle.h>
#include <qgsfeatureiterator.h>
#include <qgsfeaturerequest.h>

// P1: project + layer tree
#include <qgsproject.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreenode.h>

// P1: expression
#include <qgsexpression.h>
#include <qgsexpressioncontext.h>
#include <qgsexpressioncontextutils.h>

namespace py = pybind11;

// ── P0 helpers ────────────────────────────────────────────────────────────────

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

// ── Module ────────────────────────────────────────────────────────────────────

PYBIND11_MODULE(_antigravity_core, m) {

    // ── P0 functions ──────────────────────────────────────────────────────────
    m.def("init", [](const std::string &root) {
        antigravity_init(QString::fromStdString(root));
        py::module_::import("atexit").attr("register")(
            py::cpp_function([]() {
                QgsCoordinateTransform::invalidateCache(true);
            })
        );
    });
    m.def("open_raster", &open_raster);
    m.def("render_to_png", &render_to_png);

    // ── P1: QgsRectangle ──────────────────────────────────────────────────────
    py::class_<QgsRectangle>(m, "QgsRectangle")
        .def(py::init<>())
        .def("xMinimum", &QgsRectangle::xMinimum)
        .def("xMaximum", &QgsRectangle::xMaximum)
        .def("yMinimum", &QgsRectangle::yMinimum)
        .def("yMaximum", &QgsRectangle::yMaximum)
        .def("width",    &QgsRectangle::width)
        .def("height",   &QgsRectangle::height)
        .def("isNull",   &QgsRectangle::isNull)
        .def("__repr__", [](const QgsRectangle &r) {
            return "QgsRectangle(" + std::to_string(r.xMinimum()) + ", "
                 + std::to_string(r.yMinimum()) + ", "
                 + std::to_string(r.xMaximum()) + ", "
                 + std::to_string(r.yMaximum()) + ")";
        });

    // ── P1: QgsCRS ────────────────────────────────────────────────────────────
    py::class_<QgsCoordinateReferenceSystem>(m, "QgsCRS")
        .def(py::init<>())
        .def("isValid",     &QgsCoordinateReferenceSystem::isValid)
        .def("authid",      &QgsCoordinateReferenceSystem::authid)
        .def("description", &QgsCoordinateReferenceSystem::description);

    // ── P1: QgsGeometry ───────────────────────────────────────────────────────
    py::class_<QgsGeometry>(m, "QgsGeometry")
        .def(py::init<>())
        .def("isNull",   &QgsGeometry::isNull)
        .def("isEmpty",  &QgsGeometry::isEmpty)
        .def("wkt",      [](const QgsGeometry &g) { return g.asWkt(); })
        .def("area",     &QgsGeometry::area)
        .def("length",   &QgsGeometry::length)
        .def("type",     [](const QgsGeometry &g) { return (int)g.type(); });

    // ── P1: QgsField / QgsFields ──────────────────────────────────────────────
    py::class_<QgsField>(m, "QgsField")
        .def("name",     &QgsField::name)
        .def("typeName", &QgsField::typeName);

    py::class_<QgsFields>(m, "QgsFields")
        .def("count", &QgsFields::count)
        .def("names",  &QgsFields::names)
        .def("field",  [](const QgsFields &f, int i) { return f.field(i); });

    // ── P1: QgsFeature ────────────────────────────────────────────────────────
    py::class_<QgsFeature>(m, "QgsFeature")
        .def(py::init<>())
        .def("id",       &QgsFeature::id)
        .def("geometry", &QgsFeature::geometry)
        .def("fields",   &QgsFeature::fields)
        .def("attribute", [](const QgsFeature &f, const QString &name) {
            return f.attribute(name);
        })
        .def("attributeList", [](const QgsFeature &f) {
            return f.attributes().toList();
        });

    // ── P1: QgsMapLayer (abstract base, no Python constructor) ────────────────
    py::class_<QgsMapLayer>(m, "QgsMapLayer")
        .def("id",      &QgsMapLayer::id)
        .def("name",    &QgsMapLayer::name)
        .def("isValid", &QgsMapLayer::isValid)
        .def("crs",     &QgsMapLayer::crs)
        .def("extent",  &QgsMapLayer::extent)
        .def("type",    [](const QgsMapLayer &l) { return (int)l.type(); });

    // ── P1: QgsRasterLayer ────────────────────────────────────────────────────
    py::class_<QgsRasterLayer, QgsMapLayer>(m, "QgsRasterLayer")
        .def(py::init([](const QString &path, const QString &name) {
            return new QgsRasterLayer(path, name, "gdal");
        }), py::arg("path"), py::arg("name") = "raster",
            py::return_value_policy::take_ownership)
        .def("width",     &QgsRasterLayer::width)
        .def("height",    &QgsRasterLayer::height)
        .def("bandCount", &QgsRasterLayer::bandCount);

    // ── P1: QgsVectorLayer ────────────────────────────────────────────────────
    py::class_<QgsVectorLayer, QgsMapLayer>(m, "QgsVectorLayer")
        .def(py::init([](const QString &path, const QString &name) {
            return new QgsVectorLayer(path, name, "ogr");
        }), py::arg("path"), py::arg("name") = "vector",
            py::return_value_policy::take_ownership)
        .def("featureCount", [](const QgsVectorLayer &l) { return l.featureCount(); })
        .def("fields", [](const QgsVectorLayer &l) { return l.fields(); })
        .def("wkbType",      [](const QgsVectorLayer &l) { return (int)l.wkbType(); })
        .def("getFeatures",  [](QgsVectorLayer &l, long long limit) {
            std::vector<QgsFeature> out;
            QgsFeatureRequest req;
            if (limit > 0) req.setLimit(limit);
            QgsFeatureIterator it = l.getFeatures(req);
            QgsFeature f;
            while (it.nextFeature(f)) out.push_back(f);
            return out;
        }, py::arg("limit") = 1000LL);

    // ── P1: QgsLayerTreeNode / QgsLayerTreeGroup ──────────────────────────────
    py::class_<QgsLayerTreeNode>(m, "QgsLayerTreeNode")
        .def("name",    &QgsLayerTreeNode::name)
        .def("isGroup", [](const QgsLayerTreeNode &n) {
            return n.nodeType() == QgsLayerTreeNode::NodeGroup;
        })
        .def("isLayer", [](const QgsLayerTreeNode &n) {
            return n.nodeType() == QgsLayerTreeNode::NodeLayer;
        });

    py::class_<QgsLayerTreeGroup, QgsLayerTreeNode>(m, "QgsLayerTreeGroup")
        .def("name",     &QgsLayerTreeGroup::name)
        .def("layerIds", [](const QgsLayerTreeGroup &g) {
            QStringList ids;
            for (QgsLayerTreeLayer *n : g.findLayers()) ids << n->layerId();
            return ids;
        })
        .def("findLayer", [](QgsLayerTreeGroup &g, const QString &layerId) -> QgsMapLayer* {
            QgsLayerTreeLayer *n = g.findLayer(layerId);
            return n ? n->layer() : nullptr;
        }, py::return_value_policy::reference);

    // ── P1: QgsLayerTree (root of the project layer tree) ────────────────────
    py::class_<QgsLayerTree, QgsLayerTreeGroup>(m, "QgsLayerTree");

    // ── P1: QgsProject ────────────────────────────────────────────────────────
    py::class_<QgsProject>(m, "QgsProject")
        .def(py::init([]() { return new QgsProject(); }),
             py::return_value_policy::take_ownership)
        .def("read",  [](QgsProject &p, const QString &path) { return p.read(path); })
        .def("write", [](QgsProject &p, const QString &path) { return p.write(path); })
        .def("clear", &QgsProject::clear)
        .def("crs",   &QgsProject::crs)
        .def("title", &QgsProject::title)
        .def("fileName", &QgsProject::fileName)
        .def("addRasterLayer", [](QgsProject &p, const QString &path, const QString &name) -> QgsRasterLayer* {
            auto *l = new QgsRasterLayer(path, name, "gdal");
            if (!l->isValid()) { delete l; return nullptr; }
            p.addMapLayer(l);
            return l;
        }, py::return_value_policy::reference)
        .def("addVectorLayer", [](QgsProject &p, const QString &path, const QString &name) -> QgsVectorLayer* {
            auto *l = new QgsVectorLayer(path, name, "ogr");
            if (!l->isValid()) { delete l; return nullptr; }
            p.addMapLayer(l);
            return l;
        }, py::return_value_policy::reference)
        .def("mapLayerIds", [](const QgsProject &p) {
            return p.mapLayers().keys();
        })
        .def("layerTreeRoot", [](QgsProject &p) -> QgsLayerTree* {
            return p.layerTreeRoot();
        }, py::return_value_policy::reference);

    // ── P1: QgsExpressionContext ──────────────────────────────────────────────
    py::class_<QgsExpressionContext>(m, "QgsExpressionContext")
        .def(py::init<>())
        .def("setFeature", &QgsExpressionContext::setFeature);

    // ── P1: QgsExpression ─────────────────────────────────────────────────────
    py::class_<QgsExpression>(m, "QgsExpression")
        .def(py::init<const QString &>())
        .def("isValid",          [](const QgsExpression &e) { return !e.hasParserError(); })
        .def("hasParserError",   &QgsExpression::hasParserError)
        .def("parserErrorString",&QgsExpression::parserErrorString)
        .def("evaluate",         [](QgsExpression &e, QgsExpressionContext &ctx) -> QVariant {
            QVariant result = e.evaluate(&ctx);
            if (e.hasEvalError()) return QVariant();
            return result;
        })
        .def("evaluateFeature",  [](QgsExpression &e, QgsVectorLayer &layer,
                                     const QgsFeature &feat) -> QVariant {
            QgsExpressionContext ctx;
            ctx.appendScopes(
                QgsExpressionContextUtils::globalProjectLayerScopes(&layer));
            ctx.setFeature(feat);
            QVariant result = e.evaluate(&ctx);
            if (e.hasEvalError()) return QVariant();
            return result;
        });
}
