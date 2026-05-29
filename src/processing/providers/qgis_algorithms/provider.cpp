// src/processing/providers/qgis_algorithms/provider.cpp
#include "provider.h"

// Raster algorithms
#include "algorithms/raster/raster_calculator.h"
#include "algorithms/raster/raster_resample.h"
#include "algorithms/raster/raster_clip.h"
#include "algorithms/raster/raster_merge_bands.h"
#include "algorithms/raster/raster_ndvi.h"
#include "algorithms/raster/raster_statistics.h"

// Vector algorithms
#include "algorithms/vector/vector_buffer.h"
#include "algorithms/vector/vector_clip.h"
#include "algorithms/vector/vector_dissolve.h"
#include "algorithms/vector/vector_merge.h"
#include "algorithms/vector/vector_spatial_query.h"
#include "algorithms/vector/vector_attribute_query.h"
#include "algorithms/vector/vector_reproject.h"

#include <QIcon>

QgisAlgorithmsProvider::QgisAlgorithmsProvider()
    : QgsProcessingProvider()
{
}

QIcon QgisAlgorithmsProvider::icon() const
{
    return QIcon::fromTheme( QStringLiteral( "qgis" ) );
}

QgsProcessingProvider *QgisAlgorithmsProvider::clone() const
{
    return new QgisAlgorithmsProvider();
}

void QgisAlgorithmsProvider::loadAlgorithms()
{
    // Raster algorithms
    addAlgorithm( new RasterCalculatorAlgorithm() );
    addAlgorithm( new RasterResampleAlgorithm() );
    addAlgorithm( new RasterClipAlgorithm() );
    addAlgorithm( new RasterMergeBandsAlgorithm() );
    addAlgorithm( new RasterNdviAlgorithm() );
    addAlgorithm( new RasterStatisticsAlgorithm() );

    // Vector algorithms
    addAlgorithm( new VectorBufferAlgorithm() );
    addAlgorithm( new VectorClipAlgorithm() );
    addAlgorithm( new VectorDissolveAlgorithm() );
    addAlgorithm( new VectorMergeAlgorithm() );
    addAlgorithm( new VectorSpatialQueryAlgorithm() );
    addAlgorithm( new VectorAttributeQueryAlgorithm() );
    addAlgorithm( new VectorReprojectAlgorithm() );
}
