// nodata_utils.cpp — see nodata_utils.h for the policy contract.
#include "nodata_utils.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

namespace sicnu::rs
{

float bandNoDataSentinel( const GdalDatasetWrapper &ds, int band )
{
    bool hasNodata = false;
    const double nodataRaw = ds.bandNoDataValue( band, &hasNodata );
    if ( !hasNodata || !std::isfinite( nodataRaw ) )
        return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>( nodataRaw );
}

} // namespace sicnu::rs
