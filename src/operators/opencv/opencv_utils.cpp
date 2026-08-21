/***************************************************************************
 * opencv_utils.cpp  —  GDAL / OpenCV interop helpers
 ***************************************************************************/
#include "opencv_utils.h"

#include <gdal.h>
#include <cpl_error.h>

#include <QFile>
#include <QString>

namespace sicnu::operators::opencv {

cv::Mat readRasterBandToMat(const std::string& inputPath,
                            int band,
                            std::string* errorMessage) {
    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        if (errorMessage) *errorMessage = ds.lastError().toStdString();
        return {};
    }

    if (band < 1 || band > ds.bandCount()) {
        if (errorMessage) {
            *errorMessage = "Band index " + std::to_string(band) + " out of range (1.." +
                            std::to_string(ds.bandCount()) + ")";
        }
        return {};
    }

    const int w = ds.width();
    const int h = ds.height();
    cv::Mat mat(h, w, CV_32FC1);

    if (!ds.readBandData(band, mat.ptr<float>(), w, h)) {
        if (errorMessage) *errorMessage = "Failed to read raster band data";
        return {};
    }

    // Honor the hasNoData flag: an unset NoData returns an unspecified value
    // (0.0 in practice), and masking against it would destroy every valid 0
    // pixel (#444). Compare in float space so large sentinels match exactly.
    bool hasNodata = false;
    const double nodata = ds.bandNoDataValue(band, &hasNodata);
    if (hasNodata && std::isfinite(nodata)) {
        const float nodataF = static_cast<float>(nodata);
        float *ptr = mat.ptr<float>();
        const size_t n = static_cast<size_t>(w) * h;
        for (size_t i = 0; i < n; ++i) {
            if (!std::isfinite(ptr[i]) || ptr[i] == nodataF)
                ptr[i] = std::numeric_limits<float>::quiet_NaN();
        }
    }

    return mat;
}

std::vector<cv::Mat> readRasterBandsToMats(const std::string& inputPath,
                                           std::string* errorMessage) {
    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        if (errorMessage) *errorMessage = ds.lastError().toStdString();
        return {};
    }

    const int w = ds.width();
    const int h = ds.height();
    std::vector<cv::Mat> result;
    result.reserve(ds.bandCount());

    for (int b = 1; b <= ds.bandCount(); ++b) {
        cv::Mat bandMat(h, w, CV_32FC1);
        if (!ds.readBandData(b, bandMat.ptr<float>(), w, h)) {
            if (errorMessage) *errorMessage = "Failed to read raster band " + std::to_string(b);
            return {};
        }
        bool hasNodata = false;
        const double nodata = ds.bandNoDataValue(b, &hasNodata);
        if (hasNodata && std::isfinite(nodata)) {
            const float nodataF = static_cast<float>(nodata);
            float *ptr = bandMat.ptr<float>();
            const size_t n = static_cast<size_t>(w) * h;
            for (size_t i = 0; i < n; ++i) {
                if (!std::isfinite(ptr[i]) || ptr[i] == nodataF)
                    ptr[i] = std::numeric_limits<float>::quiet_NaN();
            }
        }
        result.push_back(std::move(bandMat));
    }

    return result;
}

bool writeMatToRaster(const std::string& outputPath,
                      const cv::Mat& mat,
                      const std::string& sourcePath,
                      std::string* errorMessage) {
    if (mat.empty()) {
        if (errorMessage) *errorMessage = "Input cv::Mat is empty";
        return false;
    }

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(sourcePath))) {
        if (errorMessage) *errorMessage = src.lastError().toStdString();
        return false;
    }

    const int w = mat.cols;
    const int h = mat.rows;
    const std::array<double, 6> geo = src.geoTransform();
    const QString projection = src.projection();

    QString qError;
    GDALDatasetH outDs = createOutputTiff(QString::fromStdString(outputPath),
                                          w, h, 1, GDT_Float32,
                                          geo, projection, &qError);
    if (!outDs) {
        if (errorMessage) *errorMessage = qError.toStdString();
        return false;
    }

    GDALRasterBandH band = GDALGetRasterBand(outDs, 1);
    if (!band) {
        if (errorMessage) *errorMessage = "Failed to get output raster band";
        GDALClose(outDs);
        return false;
    }

    // Only materialize NaN -> sentinel when the source actually declares a
    // finite NoData; with no declaration the output keeps NaN pixels and is
    // declared NoData=NaN (never a fabricated 0) (#445).
    bool srcHasNodata = false;
    const double srcNodata = src.bandNoDataValue(1, &srcHasNodata);
    const bool srcNodataValid = srcHasNodata && std::isfinite(srcNodata);
    const double outNodata = srcNodataValid ? srcNodata : std::numeric_limits<double>::quiet_NaN();
    GDALSetRasterNoDataValue(band, outNodata);

    cv::Mat bandMat = mat.isContinuous() ? mat : mat.clone();
    if (srcNodataValid) {
        const float nodataF = static_cast<float>(srcNodata);
        float *ptr = bandMat.ptr<float>();
        const size_t n = static_cast<size_t>(w) * h;
        for (size_t k = 0; k < n; ++k) {
            if (std::isnan(ptr[k]))
                ptr[k] = nodataF;
        }
    }

    CPLErr err = GDALRasterIO(band, GF_Write,
                              0, 0, w, h,
                              const_cast<float*>(bandMat.ptr<float>()),
                              w, h, GDT_Float32, 0, 0);

    GDALClose(outDs);

    if (err != CE_None) {
        if (errorMessage) *errorMessage = CPLGetLastErrorMsg();
        return false;
    }

    return true;
}

bool writeMatsToRaster(const std::string& outputPath,
                       const std::vector<cv::Mat>& mats,
                       const std::string& sourcePath,
                       std::string* errorMessage) {
    if (mats.empty()) {
        if (errorMessage) *errorMessage = "No bands to write";
        return false;
    }

    const int w = mats[0].cols;
    const int h = mats[0].rows;
    if (w <= 0 || h <= 0) {
        if (errorMessage) *errorMessage = "Invalid raster dimensions in input matrix";
        return false;
    }

    for (size_t i = 0; i < mats.size(); ++i) {
        if (mats[i].empty() || mats[i].cols != w || mats[i].rows != h) {
            if (errorMessage) {
                *errorMessage = "Band " + std::to_string(i + 1) + " dimension mismatch: expected " +
                                std::to_string(w) + "x" + std::to_string(h) + ", got " +
                                std::to_string(mats[i].cols) + "x" + std::to_string(mats[i].rows);
            }
            return false;
        }
    }

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(sourcePath))) {
        if (errorMessage) *errorMessage = src.lastError().toStdString();
        return false;
    }

    const int bandCount = static_cast<int>(mats.size());
    const std::array<double, 6> geo = src.geoTransform();
    const QString projection = src.projection();

    QString qError;
    GDALDatasetH outDs = createOutputTiff(QString::fromStdString(outputPath),
                                          w, h, bandCount, GDT_Float32,
                                          geo, projection, &qError);
    if (!outDs) {
        if (errorMessage) *errorMessage = qError.toStdString();
        return false;
    }

    for (int i = 0; i < bandCount; ++i) {
        GDALRasterBandH band = GDALGetRasterBand(outDs, i + 1);
        bool srcHasNodata = false;
        const double srcNodata = src.bandNoDataValue(i + 1, &srcHasNodata);
        const bool srcNodataValid = srcHasNodata && std::isfinite(srcNodata);
        const double outNodata = srcNodataValid ? srcNodata : std::numeric_limits<double>::quiet_NaN();
        GDALSetRasterNoDataValue(band, outNodata);

        cv::Mat bandMat = mats[i].isContinuous() ? mats[i] : mats[i].clone();
        if (srcNodataValid) {
            const float nodataF = static_cast<float>(srcNodata);
            float *ptr = bandMat.ptr<float>();
            const size_t n = static_cast<size_t>(w) * h;
            for (size_t k = 0; k < n; ++k) {
                if (std::isnan(ptr[k]))
                    ptr[k] = nodataF;
            }
        }

        CPLErr err = GDALRasterIO(band, GF_Write,
                                  0, 0, w, h,
                                  const_cast<float*>(bandMat.ptr<float>()),
                                  w, h, GDT_Float32, 0, 0);
        if (err != CE_None) {
            if (errorMessage) *errorMessage = CPLGetLastErrorMsg();
            GDALClose(outDs);
            return false;
        }
    }

    GDALClose(outDs);
    return true;
}

int rasterBandCount(const std::string& inputPath) {
    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        return 0;
    }
    return ds.bandCount();
}

bool isValidKernelSize(int kernelSize) {
    return kernelSize > 0 && (kernelSize % 2) == 1;
}

} // namespace sicnu::operators::opencv
