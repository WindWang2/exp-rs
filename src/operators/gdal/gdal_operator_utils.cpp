/***************************************************************************
 * gdal_operator_utils.cpp  —  Shared helpers for GDAL-based RSOperators
 ***************************************************************************/
#include "gdal_operator_utils.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <cpl_string.h>

namespace sicnu::operators::gdal::util {

const std::vector<std::string>& resamplingNames() {
    static const std::vector<std::string> names = {
        "nearest", "bilinear", "cubic", "cubicspline", "lanczos"
    };
    return names;
}

int CPL_STDCALL gdalProgressCallback(double dfComplete, const char* pszMessage,
                                     void* pProgressData) {
    auto* context = static_cast<RSOperatorContext*>(pProgressData);
    if (!context) return TRUE;

    try {
        context->throwIfCancelled();
    } catch (...) {
        return FALSE;
    }
    context->reportProgress(dfComplete, pszMessage ? pszMessage : "");
    return TRUE;
}

void appendGeoTiffDefaults(std::vector<std::string>& options,
                           const std::string& resampling) {
    options.emplace_back("-of");
    options.emplace_back("GTiff");
    options.emplace_back("-co");
    options.emplace_back("COMPRESS=LZW");
    options.emplace_back("-r");
    options.emplace_back(resampling);
}

std::pair<int, int> runGdalWarpOnDataset(GDALDatasetH hSrcDS,
                                         const std::string& outputPath,
                                         const std::vector<std::string>& optionStrings,
                                         RSOperatorContext& context,
                                         const std::string& logLabel) {
    if (!hSrcDS) {
        throw RSOperatorError(ErrorCode::GdalError, "Invalid source dataset for " + logLabel);
    }

    char** argv = nullptr;
    for (const auto& s : optionStrings) {
        argv = CSLAddString(argv, s.c_str());
    }

    GDALWarpAppOptions* psOptions = GDALWarpAppOptionsNew(argv, nullptr);
    CSLDestroy(argv);

    if (!psOptions) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to create GDAL warp options for " + logLabel);
    }

    GDALWarpAppOptionsSetProgress(psOptions, gdalProgressCallback, &context);

    context.logInfo(logLabel + " → " + outputPath);

    int bUsageError = FALSE;
    GDALDatasetH hDstDS = GDALWarp(outputPath.c_str(), nullptr, 1, &hSrcDS,
                                   psOptions, &bUsageError);

    GDALWarpAppOptionsFree(psOptions);

    if (!hDstDS || bUsageError) {
        throw RSOperatorError(ErrorCode::GdalError,
                              logLabel + " failed for output: " + outputPath);
    }

    const int outputWidth = GDALGetRasterXSize(hDstDS);
    const int outputHeight = GDALGetRasterYSize(hDstDS);
    GDALClose(hDstDS);

    context.reportProgress(1.0, logLabel + " complete");
    return {outputWidth, outputHeight};
}

std::pair<int, int> runGdalWarp(const std::string& inputPath,
                                const std::string& outputPath,
                                const std::vector<std::string>& optionStrings,
                                RSOperatorContext& context,
                                const std::string& logLabel) {
    ensureGdalInit();

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Input raster not found: " + inputPath);
    }

    GDALDatasetH hSrcDS = GDALOpen(inputPath.c_str(), GA_ReadOnly);
    if (!hSrcDS) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input raster: " + inputPath);
    }

    try {
        auto dims = runGdalWarpOnDataset(hSrcDS, outputPath, optionStrings, context, logLabel);
        GDALClose(hSrcDS);
        return dims;
    } catch (...) {
        GDALClose(hSrcDS);
        throw;
    }
}

} // namespace sicnu::operators::gdal::util
