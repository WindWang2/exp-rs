/***************************************************************************
 * opencv_utils.h  —  GDAL / OpenCV interop helpers for RSOperator
 ***************************************************************************/
#pragma once

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace sicnu::operators::opencv {

/**
 * Reads a single band from a raster file into a CV_32FC1 cv::Mat.
 *
 * @param inputPath  Path to the input raster.
 * @param band       1-based band index.
 * @param errorMessage Optional error message receiver.
 * @return cv::Mat on success, empty Mat on failure.
 */
cv::Mat readRasterBandToMat(const std::string& inputPath,
                            int band,
                            std::string* errorMessage = nullptr);

/**
 * Reads all bands from a raster file into a vector of CV_32FC1 cv::Mat.
 */
std::vector<cv::Mat> readRasterBandsToMats(const std::string& inputPath,
                                           std::string* errorMessage = nullptr);

/**
 * Writes a single-band CV_32FC1 cv::Mat to a GeoTIFF, copying georeferencing
 * from the source raster.
 *
 * @param outputPath Output file path.
 * @param mat        Single-band float image.
 * @param sourcePath Source raster to copy geotransform/projection from.
 * @param errorMessage Optional error message receiver.
 * @return true on success.
 */
bool writeMatToRaster(const std::string& outputPath,
                      const cv::Mat& mat,
                      const std::string& sourcePath,
                      std::string* errorMessage = nullptr);

/**
 * Writes multi-band CV_32FC1 cv::Mats to a GeoTIFF, copying georeferencing
 * from the source raster.
 */
bool writeMatsToRaster(const std::string& outputPath,
                       const std::vector<cv::Mat>& mats,
                       const std::string& sourcePath,
                       std::string* errorMessage = nullptr);

/**
 * Returns the number of bands in the raster, or 0 on failure.
 */
int rasterBandCount(const std::string& inputPath);

/**
 * Validates that a kernel size is a positive odd integer.
 */
bool isValidKernelSize(int kernelSize);

} // namespace sicnu::operators::opencv
