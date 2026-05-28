// src/python/bindings_gui.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

void init_qgis_gui(py::module_ &m)
{
    m.doc() = "QGIS GUI Python bindings";
    // Classes will be added in Task 7
}
