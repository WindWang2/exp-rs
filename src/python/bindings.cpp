// src/python/bindings.cpp
#include <pybind11/pybind11.h>

namespace py = pybind11;

// Forward declarations of sub-module init functions
void init_qgis_core(py::module_ &);
void init_qgis_gui(py::module_ &);
void init_qgis_analysis(py::module_ &);

PYBIND11_MODULE(qgis, m)
{
    m.doc() = "SICNU GEO RS Python bindings";

    auto core = m.def_submodule("core", "QGIS Core classes");
    init_qgis_core(core);

    auto gui = m.def_submodule("gui", "QGIS GUI classes");
    init_qgis_gui(gui);

    auto analysis = m.def_submodule("analysis", "QGIS Analysis classes");
    init_qgis_analysis(analysis);
}
