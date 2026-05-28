// src/python/bindings_core.cpp
// Re-enable Qt keywords before including QGIS headers.
// QT_NO_KEYWORDS is set globally by CMake for the sicnu_geo_rs target,
// but some QGIS headers still use the raw signals/slots keywords.
#include <qglobal.h>
#ifdef QT_NO_KEYWORDS
#undef QT_NO_KEYWORDS
#define _QGIS_BINDINGS_RESTORED_QT_KEYWORDS
#endif

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsmaplayer.h>
#include <qgsvectorlayer.h>
#include <qgsrasterlayer.h>
#include <qgsfeature.h>
#include <qgsfields.h>
#include <qgsfield.h>
#include <qgsgeometry.h>
#include <qgspointxy.h>
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatetransformcontext.h>
#include <qgsfeaturerequest.h>
#include <qgsfeatureiterator.h>
#include <qgswkbtypes.h>
#include <qgslogger.h>

// QString/QVariant automatic conversion casters
#include "qt_casters.h"

namespace py = pybind11;

void init_qgis_core(py::module_ &m)
{
    m.doc() = "QGIS Core Python bindings";

    // ── QgsPointXY ──
    py::class_<QgsPointXY>(m, "QgsPointXY")
        .def(py::init<>())
        .def(py::init<double, double>(), py::arg("x"), py::arg("y"))
        .def(py::init<const QgsPointXY &>())
        .def("x", &QgsPointXY::x)
        .def("y", &QgsPointXY::y)
        .def("setX", &QgsPointXY::setX)
        .def("setY", &QgsPointXY::setY)
        .def("isEmpty", &QgsPointXY::isEmpty)
        .def("__repr__", [](const QgsPointXY &p) {
            return "QgsPointXY(" + std::to_string(p.x()) + ", " + std::to_string(p.y()) + ")";
        });

    // ── QgsRectangle ──
    py::class_<QgsRectangle>(m, "QgsRectangle")
        .def(py::init<>())
        .def(py::init<double, double, double, double>(),
             py::arg("xmin"), py::arg("ymin"), py::arg("xmax"), py::arg("ymax"))
        .def(py::init<const QgsPointXY &, const QgsPointXY &>())
        .def(py::init<const QgsRectangle &>())
        .def("xMinimum", &QgsRectangle::xMinimum)
        .def("yMinimum", &QgsRectangle::yMinimum)
        .def("xMaximum", &QgsRectangle::xMaximum)
        .def("yMaximum", &QgsRectangle::yMaximum)
        .def("width", &QgsRectangle::width)
        .def("height", &QgsRectangle::height)
        .def("center", &QgsRectangle::center)
        .def("isEmpty", &QgsRectangle::isEmpty)
        .def("isNull", &QgsRectangle::isNull)
        .def("contains", py::overload_cast<const QgsPointXY &>(&QgsRectangle::contains, py::const_), py::arg("p"))
        .def("contains", py::overload_cast<const QgsRectangle &>(&QgsRectangle::contains, py::const_), py::arg("rect"))
        .def("__repr__", [](const QgsRectangle &r) {
            return "QgsRectangle(" + std::to_string(r.xMinimum()) + ", " +
                   std::to_string(r.yMinimum()) + ", " + std::to_string(r.xMaximum()) +
                   ", " + std::to_string(r.yMaximum()) + ")";
        });

    // ── QgsCoordinateReferenceSystem ──
    py::class_<QgsCoordinateReferenceSystem>(m, "QgsCoordinateReferenceSystem")
        .def(py::init<>())
        .def(py::init<const QString &>(), py::arg("srs"))
        .def(py::init<const QgsCoordinateReferenceSystem &>())
        .def("isValid", &QgsCoordinateReferenceSystem::isValid)
        .def("authid", &QgsCoordinateReferenceSystem::authid)
        .def("description", &QgsCoordinateReferenceSystem::description)
        .def("toWkt", &QgsCoordinateReferenceSystem::toWkt,
             py::arg("variant") = Qgis::CrsWktVariant::Wkt1Gdal,
             py::arg("multiline") = false,
             py::arg("indentationWidth") = 4)
        .def("createFromString", &QgsCoordinateReferenceSystem::createFromString, py::arg("definition"))
        .def_static("fromEpsgId", &QgsCoordinateReferenceSystem::fromEpsgId, py::arg("epsg"));

    // ── QgsField ──
    py::class_<QgsField>(m, "QgsField")
        .def(py::init<>())
        .def(py::init<const QString &, QMetaType::Type, const QString &, int, int, const QString &, QMetaType::Type>(),
             py::arg("name") = QString(),
             py::arg("type") = QMetaType::Type::UnknownType,
             py::arg("typeName") = QString(),
             py::arg("len") = 0,
             py::arg("prec") = 0,
             py::arg("comment") = QString(),
             py::arg("subType") = QMetaType::Type::UnknownType)
        .def(py::init<const QgsField &>())
        .def("name", &QgsField::name)
        .def("typeName", &QgsField::typeName)
        .def("type", &QgsField::type)
        .def("length", &QgsField::length)
        .def("precision", &QgsField::precision);

    // ── QgsFields ──
    py::class_<QgsFields>(m, "QgsFields")
        .def(py::init<>())
        .def(py::init<const QgsFields &>())
        .def("count", &QgsFields::count)
        .def("isEmpty", &QgsFields::isEmpty)
        .def("at", &QgsFields::at, py::arg("i"))
        .def("field", py::overload_cast<int>(&QgsFields::field, py::const_), py::arg("i"))
        .def("field", py::overload_cast<const QString &>(&QgsFields::field, py::const_), py::arg("name"))
        .def("indexOf", &QgsFields::indexOf, py::arg("name"))
        .def("names", &QgsFields::names);

    // ── QgsGeometry ──
    py::class_<QgsGeometry>(m, "QgsGeometry")
        .def(py::init<>())
        .def(py::init<const QgsGeometry &>())
        .def_static("fromPointXY", &QgsGeometry::fromPointXY, py::arg("point"))
        .def_static("fromPolygonXY", [](const QVector<QVector<QgsPointXY>> &polygon) {
            return QgsGeometry::fromPolygonXY(polygon);
        })
        .def("isNull", &QgsGeometry::isNull)
        .def("isEmpty", &QgsGeometry::isEmpty)
        .def("isMultipart", &QgsGeometry::isMultipart)
        .def("type", &QgsGeometry::type)
        .def("wkbType", &QgsGeometry::wkbType)
        .def("asWkt", &QgsGeometry::asWkt, py::arg("precision") = 17)
        .def("asJson", &QgsGeometry::asJson, py::arg("precision") = 17)
        .def("area", &QgsGeometry::area)
        .def("length", &QgsGeometry::length)
        .def("buffer", [](const QgsGeometry &g, double distance, int segments) {
            return g.buffer(distance, segments);
        }, py::arg("distance"), py::arg("segments"))
        .def("centroid", &QgsGeometry::centroid)
        .def("convexHull", &QgsGeometry::convexHull)
        .def("boundingBox", &QgsGeometry::boundingBox)
        .def("intersection", [](const QgsGeometry &g, const QgsGeometry &other) {
            return g.intersection(other);
        }, py::arg("other"))
        .def("combine", [](const QgsGeometry &g, const QgsGeometry &other) {
            return g.combine(other);
        }, py::arg("geometry"))
        .def("difference", [](const QgsGeometry &g, const QgsGeometry &other) {
            return g.difference(other);
        }, py::arg("geometry"))
        .def("simplify", &QgsGeometry::simplify, py::arg("tolerance"))
        .def("transform", [](QgsGeometry &g, const QgsCoordinateTransform &ct) {
            return g.transform(ct);
        }, py::arg("ct"));

    // ── QgsFeature ──
    py::class_<QgsFeature>(m, "QgsFeature")
        .def(py::init<>())
        .def(py::init<QgsFeatureId>(), py::arg("id"))
        .def(py::init<const QgsFeature &>())
        .def("id", &QgsFeature::id)
        .def("isValid", &QgsFeature::isValid)
        .def("fields", &QgsFeature::fields)
        .def("geometry", &QgsFeature::geometry)
        .def("setGeometry", [](QgsFeature &f, const QgsGeometry &g) {
            f.setGeometry(g);
        }, py::arg("geometry"))
        .def("hasGeometry", &QgsFeature::hasGeometry)
        .def("attribute", py::overload_cast<int>(&QgsFeature::attribute, py::const_), py::arg("fieldIdx"))
        .def("attribute", py::overload_cast<const QString &>(&QgsFeature::attribute, py::const_), py::arg("name"))
        .def("setAttribute", py::overload_cast<int, const QVariant &>(&QgsFeature::setAttribute), py::arg("fieldIdx"), py::arg("value"))
        .def("setAttribute", py::overload_cast<const QString &, const QVariant &>(&QgsFeature::setAttribute), py::arg("name"), py::arg("value"))
        .def("setFields", &QgsFeature::setFields, py::arg("fields"), py::arg("initAttributes") = true);

    // ── QgsMapLayer ──
    py::class_<QgsMapLayer>(m, "QgsMapLayer")
        .def("id", &QgsMapLayer::id)
        .def("name", &QgsMapLayer::name)
        .def("setName", &QgsMapLayer::setName)
        .def("isValid", &QgsMapLayer::isValid)
        .def("type", &QgsMapLayer::type)
        .def("crs", &QgsMapLayer::crs)
        .def("setCrs", &QgsMapLayer::setCrs, py::arg("srs"), py::arg("emitSignal") = true)
        .def("extent", &QgsMapLayer::extent)
        .def("dataProvider", [](QgsMapLayer &l) -> QgsDataProvider* {
            return l.dataProvider();
        }, py::return_value_policy::reference)
        .def("opacity", &QgsMapLayer::opacity)
        .def("setOpacity", &QgsMapLayer::setOpacity);

    // ── QgsVectorLayer ──
    py::class_<QgsVectorLayer, QgsMapLayer>(m, "QgsVectorLayer")
        .def(py::init<const QString &, const QString &, const QString &>(),
             py::arg("path"), py::arg("baseName"), py::arg("providerKey") = QStringLiteral("ogr"))
        .def("isValid", &QgsVectorLayer::isValid)
        .def("featureCount", [](QgsVectorLayer &l) {
            return l.featureCount();
        })
        .def("fields", &QgsVectorLayer::fields)
        .def("getFeatures", [](QgsVectorLayer &layer) {
            return layer.getFeatures();
        })
        .def("selectedFeatures", &QgsVectorLayer::selectedFeatures)
        .def("geometryType", &QgsVectorLayer::geometryType)
        .def("wkbType", &QgsVectorLayer::wkbType);

    // ── QgsRasterLayer ──
    py::class_<QgsRasterLayer, QgsMapLayer>(m, "QgsRasterLayer")
        .def(py::init<const QString &, const QString &, const QString &>(),
             py::arg("path"), py::arg("baseName"), py::arg("providerKey") = QStringLiteral("gdal"))
        .def("isValid", &QgsRasterLayer::isValid)
        .def("width", &QgsRasterLayer::width)
        .def("height", &QgsRasterLayer::height)
        .def("bandCount", &QgsRasterLayer::bandCount)
        .def("rasterUnitsPerPixelX", &QgsRasterLayer::rasterUnitsPerPixelX)
        .def("rasterUnitsPerPixelY", &QgsRasterLayer::rasterUnitsPerPixelY);

    // ── QgsProject ──
    py::class_<QgsProject, QObject>(m, "QgsProject")
        .def_static("instance", &QgsProject::instance, py::return_value_policy::reference)
        .def("fileName", &QgsProject::fileName)
        .def("setFileName", &QgsProject::setFileName)
        .def("title", &QgsProject::title)
        .def("setTitle", &QgsProject::setTitle)
        .def("addMapLayer", [](QgsProject &p, QgsMapLayer *layer, bool addToLegend) {
            return p.addMapLayer(layer, addToLegend, true);
        }, py::arg("layer"), py::arg("addToLegend") = true)
        .def("removeMapLayer", py::overload_cast<QgsMapLayer *>(&QgsProject::removeMapLayer))
        .def("removeMapLayer", py::overload_cast<const QString &>(&QgsProject::removeMapLayer))
        .def("mapLayers", &QgsProject::mapLayers, py::arg("validOnly") = false)
        .def("mapLayersByName", &QgsProject::mapLayersByName)
        // layerTreeRoot excluded — requires full QgsLayerTree type (Task 5+)
        .def("crs", &QgsProject::crs)
        .def("setCrs", &QgsProject::setCrs)
        .def("read", [](QgsProject &p, const QString &filename) {
            return p.read(filename);
        }, py::arg("filename"))
        .def("write", py::overload_cast<const QString &>(&QgsProject::write), py::arg("filename"));

    // ── QgsApplication ──
    py::class_<QgsApplication, QApplication>(m, "QgsApplication")
        .def_static("instance", &QgsApplication::instance, py::return_value_policy::reference)
        .def_static("setPrefixPath", &QgsApplication::setPrefixPath, py::arg("prefixPath"), py::arg("useDefaultPaths") = false)
        .def_static("initQgis", &QgsApplication::initQgis)
        .def_static("exitQgis", &QgsApplication::exitQgis);

    // ── QgsCoordinateTransform ──
    py::class_<QgsCoordinateTransform>(m, "QgsCoordinateTransform")
        .def(py::init<>())
        .def(py::init<const QgsCoordinateReferenceSystem &, const QgsCoordinateReferenceSystem &, const QgsCoordinateTransformContext &>())
        .def("transform", [](const QgsCoordinateTransform &ct, const QgsPointXY &pt) {
            return ct.transform(pt);
        })
        .def("transformBoundingBox", [](const QgsCoordinateTransform &ct, const QgsRectangle &rect, bool handle180Crossover) {
            return ct.transformBoundingBox(rect, Qgis::TransformDirection::Forward, handle180Crossover);
        }, py::arg("rect"), py::arg("handle180Crossover") = false);

    // ── QgsFeatureRequest ──
    py::class_<QgsFeatureRequest>(m, "QgsFeatureRequest")
        .def(py::init<>())
        .def(py::init<QgsFeatureId>())
        .def("setFilterRect", &QgsFeatureRequest::setFilterRect)
        .def("setFlags", &QgsFeatureRequest::setFlags);

    // ── QgsWkbTypes ──
    py::class_<QgsWkbTypes>(m, "QgsWkbTypes")
        .def_static("geometryType", &QgsWkbTypes::geometryType)
        .def_static("isMultiType", &QgsWkbTypes::isMultiType)
        .def_static("displayString", &QgsWkbTypes::displayString);
}
