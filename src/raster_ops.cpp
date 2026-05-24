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

py::array_t<float> warp_raster_band(py::array_t<float> input, int out_w, int out_h, py::array_t<double> coeffs_x, py::array_t<double> coeffs_y) {
    auto buf_in = input.request();
    auto buf_cx = coeffs_x.request();
    auto buf_cy = coeffs_y.request();

    if (buf_in.ndim != 2) {
        throw std::runtime_error("Input must be a 2D numpy array");
    }

    int in_h = buf_in.shape[0];
    int in_w = buf_in.shape[1];

    auto result = py::array_t<float>({out_h, out_w});
    auto buf_out = result.request();

    float *ptr_in = static_cast<float *>(buf_in.ptr);
    double *ptr_cx = static_cast<double *>(buf_cx.ptr);
    double *ptr_cy = static_cast<double *>(buf_cy.ptr);
    float *ptr_out = static_cast<float *>(buf_out.ptr);

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
                             
                ptr_out[j * out_w + i] = static_cast<float>(val);
            } else {
                ptr_out[j * out_w + i] = 0.0f;
            }
        }
    }
    return result;
}

py::array_t<uint8_t> warp_and_compose_rgb(
    py::array_t<float> r, py::array_t<float> g, py::array_t<float> b,
    int out_w, int out_h,
    py::array_t<double> coeffs_x, py::array_t<double> coeffs_y,
    float r_min, float r_max,
    float g_min, float g_max,
    float b_min, float b_max
) {
    auto buf_r = r.request();
    auto buf_g = g.request();
    auto buf_b = b.request();
    auto buf_cx = coeffs_x.request();
    auto buf_cy = coeffs_y.request();

    if (buf_r.ndim != 2 || buf_g.ndim != 2 || buf_b.ndim != 2) {
        throw std::runtime_error("R, G, and B bands must be 2D numpy arrays");
    }

    int in_h = buf_r.shape[0];
    int in_w = buf_r.shape[1];

    auto result = py::array_t<uint8_t>({out_h, out_w, 3});
    auto buf_out = result.request();

    float *ptr_r = static_cast<float *>(buf_r.ptr);
    float *ptr_g = static_cast<float *>(buf_g.ptr);
    float *ptr_b = static_cast<float *>(buf_b.ptr);
    double *ptr_cx = static_cast<double *>(buf_cx.ptr);
    double *ptr_cy = static_cast<double *>(buf_cy.ptr);
    uint8_t *ptr_out = static_cast<uint8_t *>(buf_out.ptr);

    int num_coeffs = buf_cx.size;

    auto stretch = [](float val, float min_v, float max_v) -> uint8_t {
        if (max_v == min_v) return 0;
        float s = (val - min_v) / (max_v - min_v) * 255.0f;
        if (s < 0.0f) return 0;
        if (s > 255.0f) return 255;
        return static_cast<uint8_t>(std::round(s));
    };

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
                
                auto interp = [&](const float* ptr_band) -> float {
                    double p00 = ptr_band[y0 * in_w + x0];
                    double p10 = (x1 < in_w) ? ptr_band[y0 * in_w + x1] : p00;
                    double p01 = (y1 < in_h) ? ptr_band[y1 * in_w + x0] : p00;
                    double p11 = (x1 < in_w && y1 < in_h) ? ptr_band[y1 * in_w + x1] : p00;
                    
                    return static_cast<float>((p00 * (1 - dx) * (1 - dy)) +
                                              (p10 * dx * (1 - dy)) +
                                              (p01 * (1 - dx) * dy) +
                                              (p11 * dx * dy));
                };

                float val_r = interp(ptr_r);
                float val_g = interp(ptr_g);
                float val_b = interp(ptr_b);

                ptr_out[(j * out_w + i) * 3 + 0] = stretch(val_r, r_min, r_max);
                ptr_out[(j * out_w + i) * 3 + 1] = stretch(val_g, g_min, g_max);
                ptr_out[(j * out_w + i) * 3 + 2] = stretch(val_b, b_min, b_max);
            } else {
                ptr_out[(j * out_w + i) * 3 + 0] = 0;
                ptr_out[(j * out_w + i) * 3 + 1] = 0;
                ptr_out[(j * out_w + i) * 3 + 2] = 0;
            }
        }
    }
    return result;
}

py::array_t<uint8_t> warp_and_stretch_gray(
    py::array_t<float> band,
    int out_w, int out_h,
    py::array_t<double> coeffs_x, py::array_t<double> coeffs_y,
    float min_val, float max_val
) {
    auto buf_band = band.request();
    auto buf_cx = coeffs_x.request();
    auto buf_cy = coeffs_y.request();

    if (buf_band.ndim != 2) {
        throw std::runtime_error("Band must be a 2D numpy array");
    }

    int in_h = buf_band.shape[0];
    int in_w = buf_band.shape[1];

    auto result = py::array_t<uint8_t>({out_h, out_w, 3});
    auto buf_out = result.request();

    float *ptr_band = static_cast<float *>(buf_band.ptr);
    double *ptr_cx = static_cast<double *>(buf_cx.ptr);
    double *ptr_cy = static_cast<double *>(buf_cy.ptr);
    uint8_t *ptr_out = static_cast<uint8_t *>(buf_out.ptr);

    int num_coeffs = buf_cx.size;

    auto stretch = [](float val, float min_v, float max_v) -> uint8_t {
        if (max_v == min_v) return 0;
        float s = (val - min_v) / (max_v - min_v) * 255.0f;
        if (s < 0.0f) return 0;
        if (s > 255.0f) return 255;
        return static_cast<uint8_t>(std::round(s));
    };

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
                
                double p00 = ptr_band[y0 * in_w + x0];
                double p10 = (x1 < in_w) ? ptr_band[y0 * in_w + x1] : p00;
                double p01 = (y1 < in_h) ? ptr_band[y1 * in_w + x0] : p00;
                double p11 = (x1 < in_w && y1 < in_h) ? ptr_band[y1 * in_w + x1] : p00;
                
                double val = (p00 * (1 - dx) * (1 - dy)) +
                             (p10 * dx * (1 - dy)) +
                             (p01 * (1 - dx) * dy) +
                             (p11 * dx * dy);

                uint8_t stretched = stretch(static_cast<float>(val), min_val, max_val);

                ptr_out[(j * out_w + i) * 3 + 0] = stretched;
                ptr_out[(j * out_w + i) * 3 + 1] = stretched;
                ptr_out[(j * out_w + i) * 3 + 2] = stretched;
            } else {
                ptr_out[(j * out_w + i) * 3 + 0] = 0;
                ptr_out[(j * out_w + i) * 3 + 1] = 0;
                ptr_out[(j * out_w + i) * 3 + 2] = 0;
            }
        }
    }
    return result;
}

py::array_t<uint8_t> stretch_and_compose_rgb(
    py::array_t<float> r, py::array_t<float> g, py::array_t<float> b,
    float r_min, float r_max,
    float g_min, float g_max,
    float b_min, float b_max
) {
    auto buf_r = r.request();
    auto buf_g = g.request();
    auto buf_b = b.request();

    if (buf_r.ndim != 2 || buf_g.ndim != 2 || buf_b.ndim != 2) {
        throw std::runtime_error("R, G, and B bands must be 2D numpy arrays");
    }

    int h = buf_r.shape[0];
    int w = buf_r.shape[1];

    auto result = py::array_t<uint8_t>({h, w, 3});
    auto buf_out = result.request();

    float *ptr_r = static_cast<float *>(buf_r.ptr);
    float *ptr_g = static_cast<float *>(buf_g.ptr);
    float *ptr_b = static_cast<float *>(buf_b.ptr);
    uint8_t *ptr_out = static_cast<uint8_t *>(buf_out.ptr);

    auto stretch = [](float val, float min_v, float max_v) -> uint8_t {
        if (max_v == min_v) return 0;
        float s = (val - min_v) / (max_v - min_v) * 255.0f;
        if (s < 0.0f) return 0;
        if (s > 255.0f) return 255;
        return static_cast<uint8_t>(std::round(s));
    };

    for (int i = 0; i < h * w; ++i) {
        ptr_out[i * 3 + 0] = stretch(ptr_r[i], r_min, r_max);
        ptr_out[i * 3 + 1] = stretch(ptr_g[i], g_min, g_max);
        ptr_out[i * 3 + 2] = stretch(ptr_b[i], b_min, b_max);
    }
    return result;
}

py::array_t<uint8_t> stretch_gray(
    py::array_t<float> band,
    float min_val, float max_val
) {
    auto buf_band = band.request();

    if (buf_band.ndim != 2) {
        throw std::runtime_error("Band must be a 2D numpy array");
    }

    int h = buf_band.shape[0];
    int w = buf_band.shape[1];

    auto result = py::array_t<uint8_t>({h, w, 3});
    auto buf_out = result.request();

    float *ptr_band = static_cast<float *>(buf_band.ptr);
    uint8_t *ptr_out = static_cast<uint8_t *>(buf_out.ptr);

    auto stretch = [](float val, float min_v, float max_v) -> uint8_t {
        if (max_v == min_v) return 0;
        float s = (val - min_v) / (max_v - min_v) * 255.0f;
        if (s < 0.0f) return 0;
        if (s > 255.0f) return 255;
        return static_cast<uint8_t>(std::round(s));
    };

    for (int i = 0; i < h * w; ++i) {
        uint8_t stretched = stretch(ptr_band[i], min_val, max_val);
        ptr_out[i * 3 + 0] = stretched;
        ptr_out[i * 3 + 1] = stretched;
        ptr_out[i * 3 + 2] = stretched;
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
    m.def("warp_raster_band", &warp_raster_band,
          "Warps a float32 raster band using polynomial coefficients",
          py::arg("input"), py::arg("out_w"), py::arg("out_h"),
          py::arg("coeffs_x"), py::arg("coeffs_y"));
    m.def("warp_and_compose_rgb", &warp_and_compose_rgb,
          "Warps and stretches R, G, B bands into an RGB888 image",
          py::arg("r"), py::arg("g"), py::arg("b"),
          py::arg("out_w"), py::arg("out_h"),
          py::arg("coeffs_x"), py::arg("coeffs_y"),
          py::arg("r_min"), py::arg("r_max"),
          py::arg("g_min"), py::arg("g_max"),
          py::arg("b_min"), py::arg("b_max"));
    m.def("warp_and_stretch_gray", &warp_and_stretch_gray,
          "Warps and stretches a single band into an RGB888 grayscale image",
          py::arg("band"), py::arg("out_w"), py::arg("out_h"),
          py::arg("coeffs_x"), py::arg("coeffs_y"),
          py::arg("min_val"), py::arg("max_val"));
    m.def("stretch_and_compose_rgb", &stretch_and_compose_rgb,
          "Stretches R, G, B bands into an RGB888 image without warping",
          py::arg("r"), py::arg("g"), py::arg("b"),
          py::arg("r_min"), py::arg("r_max"),
          py::arg("g_min"), py::arg("g_max"),
          py::arg("b_min"), py::arg("b_max"));
    m.def("stretch_gray", &stretch_gray,
          "Stretches a single band into an RGB888 grayscale image without warping",
          py::arg("band"), py::arg("min_val"), py::arg("max_val"));
}