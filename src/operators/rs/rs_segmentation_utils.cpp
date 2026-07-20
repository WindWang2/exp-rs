/***************************************************************************
 * rs_segmentation_utils.cpp
 ***************************************************************************/
#include "rs_segmentation_utils.h"

#include "operators/framework/rs_operator_error.h"

#include <gdal_alg.h>
#include <cpl_string.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sicnu::operators::rs::segutil {

void mergeSmallRegions(cv::Mat& labels, int minSize) {
    if (minSize <= 1)
        return;
    for (int pass = 0; pass < 3; ++pass) {
        std::unordered_map<int, int> counts;
        for (int y = 0; y < labels.rows; ++y) {
            const int* row = labels.ptr<int>(y);
            for (int x = 0; x < labels.cols; ++x) {
                if (row[x] > 0)
                    counts[row[x]]++;
            }
        }
        bool changed = false;
        for (int y = 0; y < labels.rows; ++y) {
            int* row = labels.ptr<int>(y);
            for (int x = 0; x < labels.cols; ++x) {
                const int id = row[x];
                if (id <= 0)
                    continue;
                auto it = counts.find(id);
                if (it == counts.end() || it->second >= minSize)
                    continue;
                int best = id;
                int bestSize = it->second;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0)
                            continue;
                        const int ny = y + dy;
                        const int nx = x + dx;
                        if (ny < 0 || nx < 0 || ny >= labels.rows || nx >= labels.cols)
                            continue;
                        const int nid = labels.at<int>(ny, nx);
                        if (nid <= 0 || nid == id)
                            continue;
                        const int nsz = counts.count(nid) ? counts[nid] : 0;
                        if (nsz > bestSize) {
                            bestSize = nsz;
                            best = nid;
                        }
                    }
                }
                if (best != id) {
                    row[x] = best;
                    changed = true;
                }
            }
        }
        if (!changed)
            break;
    }
}

cv::Mat segmentGrid(int width, int height, int cellSize, RSOperatorContext& context) {
    if (cellSize < 4)
        cellSize = 4;
    context.reportProgress(0.18, "Segmenting: grid superpixels cell=" + std::to_string(cellSize));
    cv::Mat labels(height, width, CV_32S);
    const int cellsX = (width + cellSize - 1) / cellSize;
    for (int y = 0; y < height; ++y) {
        int* row = labels.ptr<int>(y);
        const int cy = y / cellSize;
        for (int x = 0; x < width; ++x) {
            const int cx = x / cellSize;
            row[x] = cy * cellsX + cx + 1;
        }
    }
    return labels;
}

cv::Mat segmentQuantize(const std::vector<std::vector<float>>& bandData,
                        int width, int height,
                        int smoothKernel, int quantizeBins, int minRegionSize,
                        RSOperatorContext& context) {
    const size_t nPix = static_cast<size_t>(width) * static_cast<size_t>(height);
    const int nBands = static_cast<int>(bandData.size());
    std::vector<float> mean(nPix, 0.0f);
    for (int b = 0; b < nBands; ++b) {
        for (size_t i = 0; i < nPix; ++i)
            mean[i] += bandData[static_cast<size_t>(b)][i];
    }
    const float inv = 1.0f / static_cast<float>(std::max(1, nBands));
    for (size_t i = 0; i < nPix; ++i)
        mean[i] *= inv;

    if (smoothKernel < 3)
        smoothKernel = 3;
    if (smoothKernel % 2 == 0)
        ++smoothKernel;
    if (quantizeBins < 2)
        quantizeBins = 2;

    context.reportProgress(0.15, "Segmenting: smooth + quantize + CC");
    cv::Mat img(height, width, CV_32F, mean.data());
    cv::Mat smooth;
    cv::GaussianBlur(img, smooth, cv::Size(smoothKernel, smoothKernel), 0);

    double minV = 0, maxV = 0;
    cv::minMaxLoc(smooth, &minV, &maxV);
    if (maxV <= minV)
        maxV = minV + 1.0;

    cv::Mat quant(height, width, CV_8U);
    const float scale = static_cast<float>(quantizeBins - 1) / static_cast<float>(maxV - minV);
    for (int y = 0; y < height; ++y) {
        const float* srow = smooth.ptr<float>(y);
        uint8_t* qrow = quant.ptr<uint8_t>(y);
        for (int x = 0; x < width; ++x) {
            int bin = static_cast<int>((srow[x] - static_cast<float>(minV)) * scale + 0.5f);
            bin = std::clamp(bin, 0, quantizeBins - 1);
            qrow[x] = static_cast<uint8_t>(bin);
        }
    }

    cv::Mat labels;
    cv::connectedComponents(quant, labels, 8, CV_32S);
    labels += 1; // OpenCV 0-based → 1-based
    mergeSmallRegions(labels, minRegionSize);

    std::unordered_map<int, int> remap;
    int nextId = 1;
    for (int y = 0; y < height; ++y) {
        int* row = labels.ptr<int>(y);
        for (int x = 0; x < width; ++x) {
            if (row[x] <= 0)
                continue;
            auto it = remap.find(row[x]);
            if (it == remap.end()) {
                remap[row[x]] = nextId;
                row[x] = nextId++;
            } else {
                row[x] = it->second;
            }
        }
    }
    return labels;
}

std::vector<uint8_t> rasterizeGeometry(OGRGeometryH geom, int width, int height,
                                       const double gt[6]) {
    std::vector<uint8_t> mask(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    GDALDriverH memDriver = GDALGetDriverByName("MEM");
    if (!memDriver)
        throw RSOperatorError(ErrorCode::GdalError, "MEM driver unavailable");
    GDALDatasetH memDs = GDALCreate(memDriver, "", width, height, 1, GDT_Byte, nullptr);
    if (!memDs)
        throw RSOperatorError(ErrorCode::GdalError, "Failed to create MEM raster");
    GDALSetGeoTransform(memDs, const_cast<double*>(gt));
    char** options = nullptr;
    options = CSLSetNameValue(options, "ALL_TOUCHED", "TRUE");
    int bandList[1] = {1};
    double burn[1] = {1.0};
    OGRGeometryH geoms[1] = {geom};
    const CPLErr err = GDALRasterizeGeometries(
        memDs, 1, bandList, 1, geoms, nullptr, nullptr, burn, options, nullptr, nullptr);
    CSLDestroy(options);
    if (err != CE_None) {
        GDALClose(memDs);
        throw RSOperatorError(ErrorCode::GdalError, "GDALRasterizeGeometries failed");
    }
    GDALRasterBandH band = GDALGetRasterBand(memDs, 1);
    if (GDALRasterIO(band, GF_Read, 0, 0, width, height, mask.data(), width, height,
                     GDT_Byte, 0, 0) != CE_None) {
        GDALClose(memDs);
        throw RSOperatorError(ErrorCode::GdalError, "Failed to read rasterized mask");
    }
    GDALClose(memDs);
    return mask;
}

void writeLabelGeoTiff(const std::string& outputPath,
                       const cv::Mat& labels,
                       int width, int height,
                       const double gt[6],
                       const std::string& projectionWkt) {
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver)
        throw RSOperatorError(ErrorCode::GdalError, "GTiff driver missing");
    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    GDALDatasetH outDs = GDALCreate(driver, outputPath.c_str(), width, height, 1, GDT_UInt32, opts);
    CSLDestroy(opts);
    if (!outDs)
        throw RSOperatorError(ErrorCode::GdalError, "Failed to create output: " + outputPath);

    GDALSetGeoTransform(outDs, const_cast<double*>(gt));
    if (!projectionWkt.empty())
        GDALSetProjection(outDs, projectionWkt.c_str());

    std::vector<uint32_t> outBuf(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const int* row = labels.ptr<int>(y);
        for (int x = 0; x < width; ++x)
            outBuf[static_cast<size_t>(y) * width + x] =
                static_cast<uint32_t>(std::max(0, row[x]));
    }
    GDALRasterBandH band = GDALGetRasterBand(outDs, 1);
    if (GDALRasterIO(band, GF_Write, 0, 0, width, height, outBuf.data(), width, height,
                     GDT_UInt32, 0, 0) != CE_None) {
        GDALClose(outDs);
        throw RSOperatorError(ErrorCode::GdalError, "Failed to write labels");
    }
    GDALSetRasterNoDataValue(band, 0);
    GDALClose(outDs);
}

void writeByteGeoTiff(const std::string& outputPath,
                      const std::vector<uint8_t>& data,
                      int width, int height,
                      const double gt[6],
                      const std::string& projectionWkt) {
    if (data.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        throw RSOperatorError(ErrorCode::InvalidParameter, "Byte buffer size mismatch");
    }
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver)
        throw RSOperatorError(ErrorCode::GdalError, "GTiff driver missing");
    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    GDALDatasetH outDs = GDALCreate(driver, outputPath.c_str(), width, height, 1, GDT_Byte, opts);
    CSLDestroy(opts);
    if (!outDs)
        throw RSOperatorError(ErrorCode::GdalError, "Failed to create output: " + outputPath);
    GDALSetGeoTransform(outDs, const_cast<double*>(gt));
    if (!projectionWkt.empty())
        GDALSetProjection(outDs, projectionWkt.c_str());
    GDALRasterBandH band = GDALGetRasterBand(outDs, 1);
    if (GDALRasterIO(band, GF_Write, 0, 0, width, height,
                     const_cast<uint8_t*>(data.data()), width, height, GDT_Byte, 0, 0) != CE_None) {
        GDALClose(outDs);
        throw RSOperatorError(ErrorCode::GdalError, "Failed to write class map");
    }
    GDALSetRasterNoDataValue(band, 0);
    GDALClose(outDs);
}

} // namespace sicnu::operators::rs::segutil
