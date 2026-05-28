// src/python/bindings_core.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

void init_qgis_core(py::module_ &m)
{
    m.doc() = "QGIS Core Python bindings";
    // Classes will be added in Tasks 4-6
}
