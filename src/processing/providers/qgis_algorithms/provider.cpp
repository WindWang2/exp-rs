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
#include "algorithms/vector/vector_difference.h"
#include "algorithms/vector/vector_symmetrical_difference.h"
#include "algorithms/vector/vector_select_by_location.h"
#include "algorithms/vector/vector_extract_by_location.h"
#include "algorithms/vector/vector_field_calculator.h"
#include "algorithms/vector/vector_nearest_neighbor.h"
#include "algorithms/vector/vector_distance_matrix.h"

// Native algorithms (merged from sicnu_native)
#include "algorithms/native/native_algorithms.h"

#include <QIcon>

QgisAlgorithmsProvider::QgisAlgorithmsProvider()
    : QgsProcessingProvider()
{
}

QIcon QgisAlgorithmsProvider::icon() const
{
    return QIcon::fromTheme( QStringLiteral( "qgis" ) );
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
    addAlgorithm( new VectorDifferenceAlgorithm() );
    addAlgorithm( new VectorSymmetricalDifferenceAlgorithm() );
    addAlgorithm( new VectorSelectByLocationAlgorithm() );
    addAlgorithm( new VectorExtractByLocationAlgorithm() );
    addAlgorithm( new VectorFieldCalculatorAlgorithm() );
    addAlgorithm( new VectorNearestNeighborAlgorithm() );
    addAlgorithm( new VectorDistanceMatrixAlgorithm() );

    // Native algorithms (merged from sicnu_native)
    // Vector geometry
    addAlgorithm( new QgsBufferAlgorithm() );
    addAlgorithm( new QgsCentroidsAlgorithm() );
    addAlgorithm( new QgsConvexHullAlgorithm() );
    addAlgorithm( new QgsDissolveAlgorithm() );
    addAlgorithm( new QgsSimplifyAlgorithm() );
    // Vector overlay
    addAlgorithm( new QgsClipAlgorithm() );
    addAlgorithm( new QgsIntersectionAlgorithm() );
    addAlgorithm( new QgsUnionAlgorithm() );
    addAlgorithm( new QgsDifferenceAlgorithm() );
    // Vector selection
    addAlgorithm( new QgsExtractByAttributeAlgorithm() );
    // Raster analysis
    addAlgorithm( new QgsClipRasterByExtentAlgorithm() );
    addAlgorithm( new QgsRasterLayerStatisticsAlgorithm() );
    addAlgorithm( new QgsHillshadeAlgorithm() );
    // Projection
    addAlgorithm( new QgsReprojectLayerAlgorithm() );
    addAlgorithm( new QgsAssignProjectionAlgorithm() );
}
