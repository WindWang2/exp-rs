/***************************************************************************
 * gdal_operator_utils.cpp  —  Shared helpers for GDAL-based RSOperators
 ***************************************************************************/
#include "gdal_operator_utils.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <cpl_string.h>
#include <gdal_rat.h>
#include <QFile>
#include <QString>

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
        context->reportProgress(dfComplete, pszMessage ? pszMessage : "");
        return TRUE;
    } catch (...) {
        return FALSE;
    }
}

bool isCategoricalDataset(GDALDatasetH hDS) {
    if (!hDS) return false;

    // True-ish declarations. Values are lowercase-normalized so the match is
    // case-insensitive. Per GDAL/QGIS convention, a layer is categorical when
    // it is declared "thematic" (classified); "athematic" means continuous
    // (non-classified) and must NOT be treated as categorical.
    const auto isTrueStr = [](const char* val) {
        if (!val) return false;
        std::string s(val);
        for (auto& c : s) c = static_cast<char>(std::tolower(c));
        return s == "1" || s == "true" || s == "yes" || s == "thematic" || s == "categorical" || s == "classification";
    };

    // Check dataset-level metadata item "CATEGORICAL", "CLASSIFICATION", "LAYER_TYPE"
    if (isTrueStr(GDALGetMetadataItem(hDS, "CATEGORICAL", nullptr)) ||
        isTrueStr(GDALGetMetadataItem(hDS, "CLASSIFICATION", nullptr)) ||
        isTrueStr(GDALGetMetadataItem(hDS, "LAYER_TYPE", nullptr))) {
        return true;
    }

    const int bandCount = GDALGetRasterCount(hDS);
    for (int i = 1; i <= bandCount; ++i) {
        GDALRasterBandH hBand = GDALGetRasterBand(hDS, i);
        if (!hBand) continue;

        // 1. Palette Index color interpretation
        if (GDALGetRasterColorInterpretation(hBand) == GCI_PaletteIndex) {
            return true;
        }

        // 2. Color table present on single-band rasters (multi-band RGB may have
        //    spurious color tables from the TIFF driver without being categorical)
        if (bandCount == 1 && GDALGetRasterColorTable(hBand) != nullptr) {
            return true;
        }

        // 2. Raster Attribute Table (RAT) present
        if (GDALGetDefaultRAT(hBand) != nullptr) {
            return true;
        }

        // 3. Category names list present
        char** catNames = GDALGetRasterCategoryNames(hBand);
        if (catNames != nullptr && CSLCount(catNames) > 0) {
            return true;
        }

        // 4. Band-level metadata items
        if (isTrueStr(GDALGetMetadataItem(hBand, "CATEGORICAL", nullptr)) ||
            isTrueStr(GDALGetMetadataItem(hBand, "CLASSIFICATION", nullptr)) ||
            isTrueStr(GDALGetMetadataItem(hBand, "LAYER_TYPE", nullptr))) {
            return true;
        }
    }

    return false;
}

std::string enforceCategoricalResamplingRule(GDALDatasetH hDS,
                                             const std::string& requestedResampling,
                                             RSOperatorContext& context) {
    if (!isCategoricalDataset(hDS)) {
        return requestedResampling;
    }

    // Continuous resampling methods that blur discrete category values
    if (requestedResampling == "bilinear" || requestedResampling == "cubic" ||
        requestedResampling == "cubicspline" || requestedResampling == "lanczos") {
        context.logWarning("Categorical raster detected. Resampling method overridden from '" +
                           requestedResampling + "' to 'nearest' to preserve discrete category values.");
        return "nearest";
    }

    return requestedResampling;
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

    // Enforce categorical resampling rule on optionStrings if continuous resampling is requested
    std::vector<std::string> effectiveOptions = optionStrings;
    if (isCategoricalDataset(hSrcDS)) {
        for (size_t i = 0; i < effectiveOptions.size(); ++i) {
            if (effectiveOptions[i] == "-r" && i + 1 < effectiveOptions.size()) {
                effectiveOptions[i + 1] = enforceCategoricalResamplingRule(hSrcDS, effectiveOptions[i + 1], context);
            }
        }
    }

    char** argv = nullptr;
    for (const auto& s : effectiveOptions) {
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
    GDALDatasetH hDstDS = nullptr;
    try {
        hDstDS = GDALWarp(outputPath.c_str(), nullptr, 1, &hSrcDS, psOptions, &bUsageError);
        GDALWarpAppOptionsFree(psOptions);

        if (context.isCancelled()) {
            if (hDstDS) GDALClose(hDstDS);
            QFile::remove(QString::fromStdString(outputPath));
            throw RSOperatorError(ErrorCode::Cancelled, logLabel + " cancelled");
        }

        if (!hDstDS || bUsageError) {
            if (hDstDS) GDALClose(hDstDS);
            QFile::remove(QString::fromStdString(outputPath));
            throw RSOperatorError(ErrorCode::GdalError,
                                  logLabel + " failed for output: " + outputPath);
        }

        const int outputWidth = GDALGetRasterXSize(hDstDS);
        const int outputHeight = GDALGetRasterYSize(hDstDS);
        GDALClose(hDstDS);

        context.reportProgress(1.0, logLabel + " complete");
        return {outputWidth, outputHeight};
    } catch (...) {
        if (hDstDS) GDALClose(hDstDS);
        QFile::remove(QString::fromStdString(outputPath));
        throw;
    }
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
