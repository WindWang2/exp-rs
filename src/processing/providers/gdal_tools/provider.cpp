// src/processing/providers/gdal_tools/provider.cpp
#include "provider.h"
#include "algorithms/gdal_translate.h"
#include "algorithms/gdal_warp.h"
#include "algorithms/gdal_info.h"

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
    addAlgorithm(new GdalTranslateAlgorithm());
    addAlgorithm(new GdalWarpAlgorithm());
    addAlgorithm(new GdalInfoAlgorithm());
}
