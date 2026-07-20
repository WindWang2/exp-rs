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

    return mat;
}

std::vector<cv::Mat> readRasterBandsToMats(const std::string& inputPath,
                                           std::string* errorMessage) {
    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        if (errorMessage) *errorMessage = ds.lastError().toStdString();
        return {};
    }

    std::vector<cv::Mat> result;
    result.reserve(ds.bandCount());

    for (int b = 1; b <= ds.bandCount(); ++b) {
        cv::Mat bandMat = readRasterBandToMat(inputPath, b, errorMessage);
        if (bandMat.empty()) {
            return {};
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

    CPLErr err = GDALRasterIO(band, GF_Write,
                              0, 0, w, h,
                              const_cast<float*>(mat.ptr<float>()),
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

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(sourcePath))) {
        if (errorMessage) *errorMessage = src.lastError().toStdString();
        return false;
    }

    const int w = mats[0].cols;
    const int h = mats[0].rows;
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
        CPLErr err = GDALRasterIO(band, GF_Write,
                                  0, 0, w, h,
                                  const_cast<float*>(mats[i].ptr<float>()),
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
