/***************************************************************************
 * gdal_operator_utils.h  —  Shared helpers for GDAL-based RSOperators
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <json/json.h>

#include <gdal.h>
#include <gdal_utils.h>

#include <string>
#include <utility>
#include <vector>

namespace sicnu::operators::gdal::util {

// Re-export shared JSON helpers under gdal::util for existing call sites.
using params::fileExists;
using params::requireString;
using params::getString;
using params::getInt;
using params::getDouble;
using params::getBool;
using params::hasNumber;
using params::getEnum;
using params::getStringArray;

/** Standard GDALWarp resampling names. */
const std::vector<std::string>& resamplingNames();

/**
 * GDALWarp progress adapter → RSOperatorContext.
 * Returns FALSE if cancelled (cooperative cancel).
 */
int CPL_STDCALL gdalProgressCallback(double dfComplete, const char* pszMessage,
                                     void* pProgressData);

/**
 * Run GDALWarp with pre-built option strings (no leading program name).
 *
 * Opens @p inputPath read-only, warps into @p outputPath, closes datasets.
 * Reports progress/cancel through @p context.
 *
 * @return (width, height) of the output dataset.
 * @throws RSOperatorError on open/option/warp failure.
 */
std::pair<int, int> runGdalWarp(const std::string& inputPath,
                                const std::string& outputPath,
                                const std::vector<std::string>& optionStrings,
                                RSOperatorContext& context,
                                const std::string& logLabel = "GDALWarp");

/**
 * Like runGdalWarp but operates on an already-open source dataset.
 * Does not close @p hSrcDS.
 */
std::pair<int, int> runGdalWarpOnDataset(GDALDatasetH hSrcDS,
                                         const std::string& outputPath,
                                         const std::vector<std::string>& optionStrings,
                                         RSOperatorContext& context,
                                         const std::string& logLabel = "GDALWarp");

/** Append common GeoTIFF creation options into option list. */
void appendGeoTiffDefaults(std::vector<std::string>& options,
                           const std::string& resampling);

/**
 * Detect whether a GDAL dataset is categorical / discrete (e.g. land cover map,
 * classification output, palette-indexed raster, or raster with RAT / category names).
 *
 * Truthy metadata: CATEGORICAL / CLASSIFICATION / LAYER_TYPE values "1",
 * "true", "yes", "thematic", "categorical", "classification" (case-insensitive).
 * Per GDAL/QGIS convention LAYER_TYPE "athematic" means continuous
 * (non-classified) and is NOT treated as categorical.
 */
bool isCategoricalDataset(GDALDatasetH hDS);

/**
 * Validate and enforce resampling rules. If dataset is categorical and a continuous
 * resampling method (bilinear, cubic, cubicspline, lanczos) was requested, override
 * resampling to "nearest" and log a warning to context. Returns effective resampling.
 */
std::string enforceCategoricalResamplingRule(GDALDatasetH hDS,
                                             const std::string& requestedResampling,
                                             RSOperatorContext& context);

} // namespace sicnu::operators::gdal::util
