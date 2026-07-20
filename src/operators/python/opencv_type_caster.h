/***************************************************************************
 * opencv_type_caster.h  —  pybind11 type caster for cv::Mat <-> ndarray
 ***************************************************************************/
#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <opencv2/core.hpp>

#include <stdexcept>
#include <string>

namespace py = pybind11;

/**
 * @file
 *
 * pybind11 custom type caster that maps `cv::Mat` to/from NumPy `ndarray`
 * without copying pixel data.
 *
 * Both directions share the underlying buffer:
 *   - NumPy -> cv::Mat: the cv::Mat header points into the ndarray buffer.
 *     The ndarray object is guaranteed alive for the duration of the C++ call.
 *   - cv::Mat -> NumPy: a copy of the cv::Mat header is stored inside a
 *     pybind11 capsule attached to the ndarray. The capsule deletes the header
 *     when the ndarray is garbage-collected, releasing the OpenCV reference
 *     count on the shared data buffer. This is zero-copy and safe as long as
 *     the original cv::Mat uses OpenCV's reference-counted storage.
 *
 * Only C-contiguous 2-D (grayscale) and 3-D (multi-channel) arrays are
 * supported in the NumPy -> cv::Mat direction.
 */
namespace pybind11 { namespace detail {

template <> struct type_caster<cv::Mat> {
public:
    PYBIND11_TYPE_CASTER(cv::Mat, const_name("numpy.ndarray"));

    bool load(handle src, bool /*convert*/) {
        if (!isinstance<array>(src)) {
            return false;
        }

        array arr = reinterpret_borrow<array>(src);
        buffer_info info = arr.request();

        if (info.ndim < 2 || info.ndim > 3) {
            throw std::runtime_error("cv::Mat requires a 2-D or 3-D numpy array");
        }

        const int rows = static_cast<int>(info.shape[0]);
        const int cols = static_cast<int>(info.shape[1]);
        const int channels = info.ndim == 3 ? static_cast<int>(info.shape[2]) : 1;

        if (channels < 1 || channels > 512) {
            throw std::runtime_error("Invalid channel count for cv::Mat");
        }

        const int cvType = formatToCvType(info.format, channels);
        if (cvType < 0) {
            throw std::runtime_error("Unsupported numpy dtype for cv::Mat: " + info.format);
        }

        // Require row-major (C-contiguous) layout. OpenCV rows must be
        // contiguously laid out in memory.
        const size_t elemSize = cv::Mat(1, 1, cvType).elemSize();
        const size_t expectedRowStride = cols * elemSize;
        if (info.strides.empty() || info.strides[0] != static_cast<ssize_t>(expectedRowStride)) {
            throw std::runtime_error("cv::Mat only supports C-contiguous numpy arrays");
        }

        value = cv::Mat(rows, cols, cvType, info.ptr, info.strides[0]);
        return true;
    }

    static handle cast(const cv::Mat& m, return_value_policy /*policy*/, handle /*parent*/) {
        if (m.empty()) {
            return py::array().release();
        }

        const int depth = CV_MAT_DEPTH(m.type());
        const int channels = CV_MAT_CN(m.type());

        py::dtype dt = depthToDtype(depth);
        if (dt.kind() == '\0') {
            throw std::runtime_error("Unsupported cv::Mat depth for numpy conversion");
        }

        std::vector<size_t> shape;
        std::vector<size_t> strides;
        if (channels > 1) {
            shape = {static_cast<size_t>(m.rows), static_cast<size_t>(m.cols), static_cast<size_t>(channels)};
            strides = {static_cast<size_t>(m.step[0]), static_cast<size_t>(m.step[1]), static_cast<size_t>(dt.itemsize())};
        } else {
            shape = {static_cast<size_t>(m.rows), static_cast<size_t>(m.cols)};
            strides = {static_cast<size_t>(m.step[0]), static_cast<size_t>(m.step[1])};
        }

        // Store a copy of the cv::Mat header in a capsule. The copy shares the
        // pixel data and increments OpenCV's internal reference count, keeping
        // the buffer alive for the lifetime of the returned ndarray.
        cv::Mat* ownedHeader = new cv::Mat(m);
        py::capsule capsule(ownedHeader, [](void* p) {
            delete static_cast<cv::Mat*>(p);
        });

        py::array array(dt, shape, strides, m.data, capsule);
        return array.release();
    }

private:
    static int formatToCvType(const std::string& format, int channels) {
        if (format == pybind11::format_descriptor<uint8_t>::format()) return CV_8UC(channels);
        if (format == pybind11::format_descriptor<int8_t>::format()) return CV_8SC(channels);
        if (format == pybind11::format_descriptor<uint16_t>::format()) return CV_16UC(channels);
        if (format == pybind11::format_descriptor<int16_t>::format()) return CV_16SC(channels);
        if (format == pybind11::format_descriptor<int32_t>::format()) return CV_32SC(channels);
        if (format == pybind11::format_descriptor<float>::format()) return CV_32FC(channels);
        if (format == pybind11::format_descriptor<double>::format()) return CV_64FC(channels);
        return -1;
    }

    static pybind11::dtype depthToDtype(int depth) {
        switch (depth) {
            case CV_8U:  return pybind11::dtype::of<uint8_t>();
            case CV_8S:  return pybind11::dtype::of<int8_t>();
            case CV_16U: return pybind11::dtype::of<uint16_t>();
            case CV_16S: return pybind11::dtype::of<int16_t>();
            case CV_32S: return pybind11::dtype::of<int32_t>();
            case CV_32F: return pybind11::dtype::of<float>();
            case CV_64F: return pybind11::dtype::of<double>();
            default:     return pybind11::dtype();
        }
    }
};

}} // namespace pybind11::detail
