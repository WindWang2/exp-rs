// src/processing/providers/gdal_tools/provider.cpp
#include "provider.h"
#include "algorithms/gdal_translate.h"
#include "algorithms/gdal_warp.h"
#include "algorithms/gdal_info.h"
#include "algorithms/gdal_dem.h"
#include "algorithms/gdal_contour.h"
#include "algorithms/gdal_polygonize.h"
#include "algorithms/gdal_merge.h"
#include "algorithms/gdal_calc.h"
#include "algorithms/gdal_retile.h"
#include "algorithms/gdalbuildvrt.h"
#include "algorithms/gdaltindex.h"
#include "algorithms/gdalmanage.h"
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

QgsProcessingProvider *GdalToolsProvider::clone() const
{
    return new GdalToolsProvider();
}

void GdalToolsProvider::loadAlgorithms()
{
    // Raster Conversion
    addAlgorithm(new GdalTranslateAlgorithm());
    addAlgorithm(new GdalContourAlgorithm());
    addAlgorithm(new GdalPolygonizeAlgorithm());
    addAlgorithm(new GdalMergeAlgorithm());
    addAlgorithm(new GdalRetileAlgorithm());
    addAlgorithm(new GdalBuildVrtAlgorithm());
    addAlgorithm(new GdalTindexAlgorithm());

    // Raster Transformation
    addAlgorithm(new GdalWarpAlgorithm());

    // Raster Analysis
    addAlgorithm(new GdalDemAlgorithm());
    addAlgorithm(new GdalCalcAlgorithm());
    addAlgorithm(new GdalManageAlgorithm());

    // Raster Information
    addAlgorithm(new GdalInfoAlgorithm());

    // Vector Conversion
    addAlgorithm(new Ogr2OgrAlgorithm());
    addAlgorithm(new OgrTindexAlgorithm());

    // Vector Information
    addAlgorithm(new OgrInfoAlgorithm());
}
