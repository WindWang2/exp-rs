// src/processing/providers/gdal_tools/provider.cpp
#include "provider.h"
#include "processing/providers/generic_cli/generic_cli_algorithm.h"
#include "processing/tools/cli_tool_discovery.h"
#include "algorithms/gdal_translate.h"
#include "algorithms/gdal_warp.h"
#include "algorithms/gdal_info.h"
#include "algorithms/gdal_dem.h"
#include "algorithms/gdal_contour.h"
#include "algorithms/gdal_polygonize.h"
#include "algorithms/gdal_merge.h"
#include "algorithms/gdal_calc.h"
#include "algorithms/gdal_retile.h"
#include "algorithms/gdal_proximity.h"
#include "algorithms/gdal_sieve.h"
#include "algorithms/gdal_fillnodata.h"
#include "algorithms/gdal_grid.h"
#include "algorithms/gdal_rasterize.h"
#include "algorithms/gdalbuildvrt.h"
#include "algorithms/gdaltindex.h"
#include "algorithms/gdalmanage.h"
#include "algorithms/gdaladdo.h"
#include "algorithms/gdaltransform.h"
#include "algorithms/gdal_edit.h"
#include "algorithms/pct2rgb.h"
#include "algorithms/rgb2pct.h"
#include "algorithms/gdal2xyz.h"
#include "algorithms/ogr2ogr.h"
#include "algorithms/ogrinfo.h"
#include "algorithms/ogrtindex.h"

#include <QIcon>

GdalToolsProvider::GdalToolsProvider()
    : QgsProcessingProvider()
{
}

QIcon GdalToolsProvider::icon() const
{
    return QIcon::fromTheme("gdal");
}

void GdalToolsProvider::loadAlgorithms()
{
    // Raster Conversion
    addAlgorithm(new GdalTranslateAlgorithm());
    addAlgorithm(new GdalContourAlgorithm());
    addAlgorithm(new GdalPolygonizeAlgorithm());
    addAlgorithm(new GdalMergeAlgorithm());
    addAlgorithm(new GdalRetileAlgorithm());
    addAlgorithm(new GdalGridAlgorithm());
    addAlgorithm(new GdalRasterizeAlgorithm());
    addAlgorithm(new GdalBuildVrtAlgorithm());
    addAlgorithm(new GdalTindexAlgorithm());
    addAlgorithm(new GdalAddoAlgorithm());
    addAlgorithm(new Pct2RgbAlgorithm());
    addAlgorithm(new Rgb2PctAlgorithm());
    addAlgorithm(new Gdal2XyzAlgorithm());

    // Raster Transformation
    addAlgorithm(new GdalWarpAlgorithm());
    addAlgorithm(new GdalTransformAlgorithm());

    // Raster Analysis
    addAlgorithm(new GdalDemAlgorithm());
    addAlgorithm(new GdalCalcAlgorithm());
    addAlgorithm(new GdalProximityAlgorithm());
    addAlgorithm(new GdalSieveAlgorithm());
    addAlgorithm(new GdalFillNodataAlgorithm());
    addAlgorithm(new GdalManageAlgorithm());

    // Raster Information
    addAlgorithm(new GdalInfoAlgorithm());
    addAlgorithm(new GdalEditAlgorithm());

    // Vector Conversion
    addAlgorithm(new Ogr2OgrAlgorithm());
    addAlgorithm(new OgrTindexAlgorithm());

    // Vector Information
    addAlgorithm(new OgrInfoAlgorithm());

    // Auto-discovered GDAL/OGR CLI tools not covered by handcrafted wrappers
    for ( const QString &toolName : CliToolDiscovery::discoverGdalToolNames() )
    {
        addAlgorithm( new GenericCliAlgorithm(
            CliToolDiscovery::makeGdalDiscoveredConfig( toolName ), id() ) );
    }
}
