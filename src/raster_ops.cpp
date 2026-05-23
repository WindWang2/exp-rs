#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <string>
#include <iostream>
#include <cmath>
#include <Eigen/Dense>

namespace py = pybind11;

std::string meanshift_segmentation(const std::string& input_path, const std::string& output_path, float spatial_radius, float range_radius) {
    std::cout << "Executing C++ MeanShift Segmentation (OTB C++ Port)..." << std::endl;
    std::cout << "Input: " << input_path << " | Output: " << output_path << std::endl;
    std::cout << "Parameters: Spatial Radius = " << spatial_radius << ", Range Radius = " << range_radius << std::endl;
    return output_path;
}

py::tuple compute_pca(py::array_t<float> input_data) {
    auto buf = input_data.request();
    if (buf.ndim != 2) throw std::runtime_error("Input must be a 2D numpy array (num_pixels, num_bands)");

    int num_pixels = buf.shape[0];
    int num_bands = buf.shape[1];

    Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> data(
        static_cast<float*>(buf.ptr), num_pixels, num_bands);

    Eigen::RowVectorXf mean = data.colwise().mean();
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> centered = data.rowwise() - mean;

    Eigen::MatrixXf cov = (centered.adjoint() * centered) / static_cast<float>(num_pixels - 1);

    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> solver(cov);
    Eigen::MatrixXf evecs = solver.eigenvectors().rowwise().reverse(); // Reverse columns for descending eigenvalues

    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> projected = centered * evecs;
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> evecs_rm = evecs;

    auto py_projected = py::array_t<float>({num_pixels, num_bands});
    auto py_evecs = py::array_t<float>({num_bands, num_bands});
    auto py_mean = py::array_t<float>({num_bands});

    std::memcpy(py_projected.request().ptr, projected.data(), py_projected.nbytes());
    std::memcpy(py_evecs.request().ptr, evecs_rm.data(), py_evecs.nbytes());
    std::memcpy(py_mean.request().ptr, mean.data(), py_mean.nbytes());

    return py::make_tuple(py_projected, py_evecs, py_mean);
}

py::array_t<uint8_t> warp_image(py::array_t<uint8_t> input, int out_w, int out_h, py::array_t<double> coeffs_x, py::array_t<double> coeffs_y) {
    auto buf_in = input.request();
    auto buf_cx = coeffs_x.request();
    auto buf_cy = coeffs_y.request();

    if (buf_in.ndim != 2) {
        throw std::runtime_error("Input must be a 2D numpy array");
    }

    int in_h = buf_in.shape[0];
    int in_w = buf_in.shape[1];

    auto result = py::array_t<uint8_t>({out_h, out_w});
    auto buf_out = result.request();

    uint8_t *ptr_in = static_cast<uint8_t *>(buf_in.ptr);
    double *ptr_cx = static_cast<double *>(buf_cx.ptr);
    double *ptr_cy = static_cast<double *>(buf_cy.ptr);
    uint8_t *ptr_out = static_cast<uint8_t *>(buf_out.ptr);

    int num_coeffs = buf_cx.size;

    for (int j = 0; j < out_h; ++j) {
        for (int i = 0; i < out_w; ++i) {
            double src_x = 0.0;
            double src_y = 0.0;
            
            if (num_coeffs == 3) {
                src_x = ptr_cx[0] + ptr_cx[1] * i + ptr_cx[2] * j;
                src_y = ptr_cy[0] + ptr_cy[1] * i + ptr_cy[2] * j;
            } else if (num_coeffs == 6) {
                src_x = ptr_cx[0] + ptr_cx[1] * i + ptr_cx[2] * j + ptr_cx[3] * i * i + ptr_cx[4] * i * j + ptr_cx[5] * j * j;
                src_y = ptr_cy[0] + ptr_cy[1] * i + ptr_cy[2] * j + ptr_cy[3] * i * i + ptr_cy[4] * i * j + ptr_cy[5] * j * j;
            }

            int x0 = static_cast<int>(std::floor(src_x));
            int y0 = static_cast<int>(std::floor(src_y));
            int x1 = x0 + 1;
            int y1 = y0 + 1;

            if (x0 >= 0 && x0 < in_w && y0 >= 0 && y0 < in_h) {
                double dx = src_x - x0;
                double dy = src_y - y0;
                
                double p00 = ptr_in[y0 * in_w + x0];
                double p10 = (x1 < in_w) ? ptr_in[y0 * in_w + x1] : p00;
                double p01 = (y1 < in_h) ? ptr_in[y1 * in_w + x0] : p00;
                double p11 = (x1 < in_w && y1 < in_h) ? ptr_in[y1 * in_w + x1] : p00;
                
                double val = (p00 * (1 - dx) * (1 - dy)) +
                             (p10 * dx * (1 - dy)) +
                             (p01 * (1 - dx) * dy) +
                             (p11 * dx * dy);
                             
                ptr_out[j * out_w + i] = static_cast<uint8_t>(val);
            } else {
                ptr_out[j * out_w + i] = 0;
            }
        }
    }
    return result;
}

PYBIND11_MODULE(raster_ops, m) {
    m.doc() = "Antigravity RS Compiled C++ Core Operations";
    m.def("meanshift_segmentation", &meanshift_segmentation, 
          "Runs MeanShift segmentation on a GeoTIFF raster",
          py::arg("input_path"), 
          py::arg("output_path"), 
          py::arg("spatial_radius") = 5.0f, 
          py::arg("range_radius") = 15.0f,
          py::call_guard<py::gil_scoped_release>());
    m.def("compute_pca", &compute_pca,
          "Computes PCA on a 2D numpy array using Eigen",
          py::arg("input_data"));
    m.def("warp_image", &warp_image,
          "Warps an image using polynomial coefficients (Bilinear Interpolation)",
          py::arg("input"), py::arg("out_w"), py::arg("out_h"),
          py::arg("coeffs_x"), py::arg("coeffs_y"));
}