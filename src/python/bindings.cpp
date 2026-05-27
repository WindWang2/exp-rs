#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "qt_casters.h"
#include "antigravity_init.h"

// Core rendering (P0)
#include <qgsrasterlayer.h>
#include <qgsmapsettings.h>
#include <qgsmaprenderersequentialjob.h>
#include <qgsmaprenderercustompainterjob.h>
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

// P2: symbology
#include <qgssymbol.h>
#include <qgslinesymbol.h>
#include <qgsfillsymbol.h>
#include <qgsmarkersymbol.h>
#include <qgsrenderer.h>
#include <qgssinglesymbolrenderer.h>
#include <QColor>

// P2: render context + labeling
#include <qgsrendercontext.h>
#include <qgspallabeling.h>
#include <qgsvectorlayerlabeling.h>

// P3: GUI
#include <qgsmapcanvas.h>
#include <qgsmaptool.h>
#include <maptools/qgsmaptoolpan.h>
#include <maptools/qgsmaptoolzoom.h>
#include <layertree/qgslayertreeview.h>
#include <QTimer>
#include <QCoreApplication>

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
        .def(py::init<double, double, double, double>(),
             py::arg("xmin"), py::arg("ymin"), py::arg("xmax"), py::arg("ymax"))
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
        .def("renderer", [](QgsVectorLayer &l) -> QgsFeatureRenderer* {
            return l.renderer();
        }, py::return_value_policy::reference)
        .def("labeling", [](const QgsVectorLayer &l) -> py::object {
            const auto *lab = l.labeling();
            if (!lab) return py::none();
            const auto *simple = dynamic_cast<const QgsVectorLayerSimpleLabeling*>(lab);
            if (!simple) return py::none();
            return py::cast(simple->settings());
        })
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

    // ── P2: QgsRenderContext ──────────────────────────────────────────────────
    py::class_<QgsRenderContext>(m, "QgsRenderContext")
        .def(py::init<>())
        .def("scaleFactor",   &QgsRenderContext::scaleFactor)
        .def("rendererScale", &QgsRenderContext::rendererScale)
        .def("isValid", [](const QgsRenderContext &r) {
            return r.painter() != nullptr;
        });

    // ── P2: QgsSymbol hierarchy ───────────────────────────────────────────────
    py::class_<QgsSymbol>(m, "QgsSymbol")
        .def("type",    [](const QgsSymbol &s) { return (int)s.type(); })
        .def("opacity", &QgsSymbol::opacity)
        .def("color",   [](const QgsSymbol &s) {
            QColor c = s.color();
            return py::make_tuple(c.red(), c.green(), c.blue(), c.alpha());
        });

    py::class_<QgsLineSymbol,   QgsSymbol>(m, "QgsLineSymbol");
    py::class_<QgsFillSymbol,   QgsSymbol>(m, "QgsFillSymbol");
    py::class_<QgsMarkerSymbol, QgsSymbol>(m, "QgsMarkerSymbol");

    // ── P2: QgsFeatureRenderer ────────────────────────────────────────────────
    py::class_<QgsFeatureRenderer>(m, "QgsFeatureRenderer")
        .def("type", &QgsFeatureRenderer::type);

    py::class_<QgsSingleSymbolRenderer, QgsFeatureRenderer>(m, "QgsSingleSymbolRenderer")
        .def("symbol", &QgsSingleSymbolRenderer::symbol,
             py::return_value_policy::reference);

    // ── P2: QgsPalLayerSettings ───────────────────────────────────────────────
    py::class_<QgsPalLayerSettings>(m, "QgsPalLayerSettings")
        .def(py::init<>())
        .def_readwrite("drawLabels", &QgsPalLayerSettings::drawLabels)
        .def_readwrite("fieldName",  &QgsPalLayerSettings::fieldName);

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

    // ── P3: QgsMapCanvas ──────────────────────────────────────────────────────
    // mRefreshTimer is new QTimer(this) — a heap child destroyed late in ~QObject,
    // AFTER ~QWidget has already run its event processing.  Deleting timer children
    // explicitly here kills their timer IDs and disconnects timeout→refreshMap
    // BEFORE ~QWidget gets a chance to fire them mid-destruction.
    struct SafeDeleter {
        void operator()(QgsMapCanvas *c) const {
            if (!c) return;
            // Delete each QTimer child: ~QTimer kills the timer ID and
            // ~QObject disconnects timeout→refreshMap; the child removes
            // itself from c's children list, so delete c won't touch it again.
            const auto timers = c->findChildren<QTimer*>();
            for (auto *t : timers) {
                delete t;
            }
            // Cancel any render job that fired before we got here.
            c->stopRendering();
            delete c;
        }
    };
    using CanvasHolder = std::unique_ptr<QgsMapCanvas, SafeDeleter>;

    py::class_<QgsMapCanvas, CanvasHolder>(m, "QgsMapCanvas")
        .def(py::init([]() {
            return CanvasHolder(new QgsMapCanvas());
        }))
        .def("setLayers", [](QgsMapCanvas &c, const std::vector<QgsMapLayer*> &layers) {
            QList<QgsMapLayer*> lst;
            for (auto *l : layers) lst.append(l);
            c.setLayers(lst);
        })
        .def("setExtent",           [](QgsMapCanvas &c, const QgsRectangle &r) { c.setExtent(r); })
        .def("extent",              &QgsMapCanvas::extent)
        .def("setMapTool",          &QgsMapCanvas::setMapTool)
        .def("mapTool",             &QgsMapCanvas::mapTool)
        .def("refresh",             &QgsMapCanvas::refresh)
        .def("scale",               &QgsMapCanvas::scale)
        .def("magnificationFactor", &QgsMapCanvas::magnificationFactor)
        .def("saveAsImage", [](QgsMapCanvas &c, const QString &path) {
            // Use CustomPainterJob::renderSynchronously() — never enters event loop,
            // so it cannot trigger mRefreshTimer or other async canvas state.
            QgsMapSettings settings = c.mapSettings();
            QSize sz = settings.outputSize();
            if (sz.isEmpty()) sz = QSize(256, 256);
            settings.setOutputSize(sz);
            QImage img(sz, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter painter(&img);
            QgsMapRendererCustomPainterJob job(settings, &painter);
            job.renderSynchronously();
            painter.end();
            return img.save(path);
        });

    // ── P3: QgsMapTool ────────────────────────────────────────────────────────
    py::class_<QgsMapTool>(m, "QgsMapTool");

    // QgsMapTool(canvas) sets canvas as Qt parent via QObject(canvas).
    // pybind11 releases keep_alive BEFORE calling the tool's C++ dtor, so
    // canvas ~QObject auto-deletes the tool as a child, then pybind11 tries
    // to delete it again → double-free crash.
    // Fix: setParent(nullptr) immediately after construction so pybind11 is
    // the sole owner.  mCanvas is a QPointer so it safely auto-nulls when
    // canvas is deleted, protecting ~QgsMapTool's unsetMapTool() call.
    py::class_<QgsMapToolPan, QgsMapTool>(m, "QgsMapToolPan")
        .def(py::init([](QgsMapCanvas *c) {
                 auto *t = new QgsMapToolPan(c);
                 t->setParent(nullptr);  // detach from canvas — pybind11 owns it
                 return t;
             }),
             py::arg("canvas"),
             py::return_value_policy::take_ownership,
             py::keep_alive<0, 1>());

    py::class_<QgsMapToolZoom, QgsMapTool>(m, "QgsMapToolZoom")
        .def(py::init([](QgsMapCanvas *c, bool zoomOut) {
                 auto *t = new QgsMapToolZoom(c, zoomOut);
                 t->setParent(nullptr);
                 return t;
             }),
             py::arg("canvas"), py::arg("zoomOut") = false,
             py::return_value_policy::take_ownership,
             py::keep_alive<0, 1>());

    // ── P3: QgsLayerTreeView ──────────────────────────────────────────────────
    py::class_<QgsLayerTreeView>(m, "QgsLayerTreeView")
        .def(py::init([]() { return new QgsLayerTreeView(); }),
             py::return_value_policy::take_ownership);
}
