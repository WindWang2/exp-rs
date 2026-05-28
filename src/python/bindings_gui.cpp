// src/python/bindings_gui.cpp
// Re-enable Qt keywords before including QGIS headers
#include <qglobal.h>
#ifdef QT_NO_KEYWORDS
#undef QT_NO_KEYWORDS
#define _QGIS_BINDINGS_RESTORED_QT_KEYWORDS
#endif

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

// GUI headers
#include <qgsmapcanvas.h>

// Core headers needed by GUI bindings
#include <qgsmapsettings.h>
#include <layertree/qgslayertreenode.h>
#include <layertree/qgslayertreegroup.h>
#include <layertree/qgslayertreelayer.h>

// QString/QVariant automatic conversion casters
#include "qt_casters.h"

namespace py = pybind11;

void init_qgis_gui(py::module_ &m)
{
    m.doc() = "QGIS GUI Python bindings";

    // ── QgsMapSettings ──
    py::class_<QgsMapSettings>(m, "QgsMapSettings")
        .def(py::init<>())
        .def("setDestinationCrs", &QgsMapSettings::setDestinationCrs)
        .def("destinationCrs", &QgsMapSettings::destinationCrs)
        .def("setExtent", &QgsMapSettings::setExtent, py::arg("rect"), py::arg("magnified") = true)
        .def("extent", &QgsMapSettings::extent)
        .def("setLayers", &QgsMapSettings::setLayers)
        .def("layers", [](const QgsMapSettings &s, bool expandGroupLayers) {
            return s.layers(expandGroupLayers);
        }, py::arg("expandGroupLayers") = false)
        .def("setOutputSize", &QgsMapSettings::setOutputSize)
        .def("outputSize", &QgsMapSettings::outputSize);

    // ── QgsMapCanvas ──
    py::class_<QgsMapCanvas, QWidget>(m, "QgsMapCanvas")
        .def(py::init<QWidget *>(), py::arg("parent") = nullptr)
        .def("setLayers", &QgsMapCanvas::setLayers)
        .def("layers", [](const QgsMapCanvas &c, bool expandGroupLayers) {
            return c.layers(expandGroupLayers);
        }, py::arg("expandGroupLayers") = false)
        .def("setExtent", &QgsMapCanvas::setExtent, py::arg("r"), py::arg("magnified") = false)
        .def("extent", &QgsMapCanvas::extent)
        .def("refresh", &QgsMapCanvas::refresh)
        .def("setDestinationCrs", &QgsMapCanvas::setDestinationCrs)
        .def("mapSettings", [](QgsMapCanvas &c) -> const QgsMapSettings & {
            return c.mapSettings();
        }, py::return_value_policy::reference)
        .def("zoomIn", &QgsMapCanvas::zoomIn)
        .def("zoomOut", &QgsMapCanvas::zoomOut)
        .def("zoomToFullExtent", &QgsMapCanvas::zoomToFullExtent);

    // ── QgsLayerTreeNode ──
    py::class_<QgsLayerTreeNode, QObject>(m, "QgsLayerTreeNode")
        .def("nodeType", &QgsLayerTreeNode::nodeType);

    // ── QgsLayerTreeGroup ──
    py::class_<QgsLayerTreeGroup, QgsLayerTreeNode>(m, "QgsLayerTreeGroup")
        .def(py::init<const QString &, bool>(),
             py::arg("name") = QString(), py::arg("checked") = true)
        .def("name", &QgsLayerTreeGroup::name)
        .def("setName", &QgsLayerTreeGroup::setName)
        .def("addLayer", &QgsLayerTreeGroup::addLayer, py::arg("layer"))
        .def("addGroup", &QgsLayerTreeGroup::addGroup, py::arg("name"))
        .def("findGroup", &QgsLayerTreeGroup::findGroup, py::arg("name"),
             py::return_value_policy::reference)
        .def("findLayers", &QgsLayerTreeGroup::findLayers)
        .def("removeChildNode", &QgsLayerTreeGroup::removeChildNode, py::arg("node"))
        .def("removeLayer", &QgsLayerTreeGroup::removeLayer, py::arg("layer"))
        .def("removeAllChildren", &QgsLayerTreeGroup::removeAllChildren)
        .def("findLayerIds", &QgsLayerTreeGroup::findLayerIds);

    // ── QgsLayerTreeLayer ──
    py::class_<QgsLayerTreeLayer, QgsLayerTreeNode>(m, "QgsLayerTreeLayer")
        .def(py::init<QgsMapLayer *>(), py::arg("layer"))
        .def("name", &QgsLayerTreeLayer::name)
        .def("layer", &QgsLayerTreeLayer::layer, py::return_value_policy::reference)
        .def("layerId", &QgsLayerTreeLayer::layerId);
}
