/***************************************************************************
 * rs_modis_import_operator.h  —  Import MODIS HDF/GeoTIFF → multi-band GeoTIFF
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Discovers a MODIS product (HDF4/HDF5 subdatasets or named GeoTIFF) and stacks
 * selected bands into a multi-band GeoTIFF. When tile indices are known
 * (hXXvYY in the filename), applies MODIS sinusoidal georeference if missing.
 *
 * Parameters:
 *   input   (string, required)  Path to .hdf/.h5 or MOD*.tif / product directory
 *   output  (string, required)  Output multi-band GeoTIFF
 *   bands   (array, optional)   Band/subdataset names; default all non-QA / sur_refl
 *
 * Returns: output, productId, spacecraft, tileH, tileV, bandCount, bands[]
 */
class RsModisImportOperator : public RSOperator {
public:
    std::string name() const override { return "rs:modis_import"; }
    std::string displayName() const override { return "MODIS Product Import"; }
    std::string group() const override { return "data-formats"; }
    std::string description() const override {
        return "Import a MODIS HDF/GeoTIFF product into a multi-band GeoTIFF "
               "(with sinusoidal tile georeference when h/v known).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
