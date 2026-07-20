/***************************************************************************
 * pybind_sicnu_operators.cpp  —  pybind11 Python module for RSOperators
 ***************************************************************************/
#include "opencv_type_caster.h"

#include <opencv2/imgproc.hpp>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/opencv/opencv_filter_operators.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <json/json.h>

#include <memory>
#include <string>
#include <vector>

namespace py = pybind11;

namespace sicnu::operators::python {

namespace {

/**
 * Convert a Json::Value to a Python object using pybind11's JSON support.
 */
py::object jsonToPyObject(const Json::Value& value) {
    return py::module::import("json").attr("loads")(value.toStyledString());
}

/**
 * Convert a Python object to a Json::Value using pybind11's JSON support.
 */
Json::Value pyObjectToJson(const py::object& obj) {
    if (obj.is_none()) {
        return Json::Value();
    }
    py::str jsonStr = py::module::import("json").attr("dumps")(obj);
    const std::string s = jsonStr.cast<std::string>();
    Json::Reader reader;
    Json::Value value;
    if (!reader.parse(s, value)) {
        throw std::runtime_error("Failed to convert Python object to JSON: " +
                                 reader.getFormattedErrorMessages());
    }
    return value;
}

} // anonymous namespace

/**
 * @brief Python module entry point for sicnu_operators.
 *
 * Exposes:
 *   - cv::Mat <-> numpy.ndarray zero-copy conversion (via type caster)
 *   - RSOperator registry introspection
 *   - Operator execution from Python
 */
PYBIND11_MODULE(_sicnu_operators, m) {
    m.doc() = "SICNU GEO RS operator bindings (pybind11 + cv::Mat/ndarray zero-copy)";

    // -------------------------------------------------------------------------
    // cv::Mat conversion helpers (also exercise the type_caster)
    // -------------------------------------------------------------------------
    m.def("to_numpy", [](const cv::Mat& mat) -> py::object {
        // The type_caster takes care of zero-copy conversion.
        return py::cast(mat);
    }, py::arg("mat"), "Convert a cv::Mat to a numpy ndarray (zero-copy).");

    m.def("to_cv_mat", [](py::array_t<uint8_t, py::array::c_style | py::array::forcecast> arr) -> cv::Mat {
        // The type_caster converts the numpy array to cv::Mat.
        return arr.cast<cv::Mat>();
    }, py::arg("array"), "Convert a numpy ndarray to a cv::Mat (zero-copy, C-contiguous uint8).");

    // -------------------------------------------------------------------------
    // Operator registry introspection
    // -------------------------------------------------------------------------
    m.def("list_operators", []() -> std::vector<std::string> {
        return RSOperatorRegistry::instance().operatorNames();
    }, "List names of all registered RSOperators.");

    m.def("operator_schema", [](const std::string& name) -> py::object {
        auto op = RSOperatorRegistry::instance().create(name);
        if (!op) {
            throw std::runtime_error("Operator not found: " + name);
        }
        return jsonToPyObject(op->schema());
    }, py::arg("name"), "Return the JSON schema of an operator as a Python dict.");

    m.def("operator_metadata", [](const std::string& name) -> py::object {
        auto op = RSOperatorRegistry::instance().create(name);
        if (!op) {
            throw std::runtime_error("Operator not found: " + name);
        }
        return jsonToPyObject(op->metadata());
    }, py::arg("name"), "Return the metadata of an operator as a Python dict.");

    // -------------------------------------------------------------------------
    // Operator execution
    // -------------------------------------------------------------------------
    m.def("run_operator", [](const std::string& name, const py::object& params) -> py::object {
        auto op = RSOperatorRegistry::instance().create(name);
        if (!op) {
            throw std::runtime_error("Operator not found: " + name);
        }

        Json::Value jsonParams = pyObjectToJson(params);
        RSOperatorContext context;

        try {
            Json::Value result = op->run(jsonParams, context);
            return jsonToPyObject(result);
        } catch (const RSOperatorError& e) {
            throw std::runtime_error("[" + std::string(errorCodeToString(e.code())) + "] " +
                                     e.message());
        }
    }, py::arg("name"), py::arg("params"),
    "Run an RSOperator by name with a dict of parameters. Returns a dict.");

    // -------------------------------------------------------------------------
    // OpenCV filter helpers for direct ndarray processing
    // -------------------------------------------------------------------------
    m.def("gaussian_blur", [](py::array_t<uint8_t, py::array::c_style | py::array::forcecast> arr,
                              int kernelSize, double sigma) -> py::object {
        cv::Mat src = arr.cast<cv::Mat>();
        cv::Mat dst;
        cv::GaussianBlur(src, dst, cv::Size(kernelSize, kernelSize), sigma);
        return py::cast(dst);
    }, py::arg("array"), py::arg("kernel_size") = 5, py::arg("sigma") = 1.0,
    "Apply Gaussian blur to a uint8 ndarray and return the result ndarray (zero-copy out).");

    m.def("sobel", [](py::array_t<uint8_t, py::array::c_style | py::array::forcecast> arr,
                      int dx, int dy, int kernelSize) -> py::object {
        cv::Mat src = arr.cast<cv::Mat>();
        cv::Mat dst;
        cv::Sobel(src, dst, CV_16S, dx, dy, kernelSize);
        return py::cast(dst);
    }, py::arg("array"), py::arg("dx") = 1, py::arg("dy") = 0, py::arg("kernel_size") = 3,
    "Apply Sobel edge detection to a uint8 ndarray and return the result ndarray (zero-copy out).");
}

} // namespace sicnu::operators::python
