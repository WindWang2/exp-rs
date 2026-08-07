// src/processing/gdal/gdal_grid_compat.cpp — shared raster-grid builder
#include "gdal_grid_compat.h"

#include "gdal_dataset_wrapper.h"
#include "data/raster_grid_compat.h"

namespace sicnu::processing
{

data::RasterGrid gridFromDataset( const GdalDatasetWrapper &ds )
{
  data::RasterGrid grid;
  grid.crsWkt = ds.projection();
  grid.hasGeoTransform = ds.hasGeoTransform();
  grid.geoTransform = ds.geoTransform();
  grid.width = ds.width();
  grid.height = ds.height();
  const int bandCount = ds.bandCount();
  for ( int b = 1; b <= bandCount; ++b )
  {
    bool hasNoData = false;
    const double noData = ds.bandNoDataValue( b, &hasNoData );
    grid.bandNoData.append( hasNoData ? std::optional<double>( noData )
                                      : std::nullopt );
  }
  return grid;
}

} // namespace sicnu::processing
