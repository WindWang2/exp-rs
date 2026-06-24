// src/processing/providers/qgis_algorithms/provider.cpp
#include "provider.h"

#include "core/sicnu_logging.h"

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
#include "algorithms/vector/vector_multipart_to_singlepart.h"
#include "algorithms/vector/vector_smooth_geometry.h"
#include "algorithms/vector/vector_fix_geometries.h"

// Remote Sensing algorithms
#include "algorithms/remote_sensing/band_math_algorithm.h"
#include "algorithms/remote_sensing/spectral_index_algorithm.h"
#include "algorithms/remote_sensing/atmospheric_correction_algorithm.h"

// Native algorithms (split from former native_algorithms.h)
#include "algorithms/native/native_centroids.h"
#include "algorithms/native/native_convex_hull.h"
#include "algorithms/native/native_simplify.h"
#include "algorithms/native/native_clip.h"
#include "algorithms/native/native_intersection.h"
#include "algorithms/native/native_union.h"
#include "algorithms/native/native_difference.h"
#include "algorithms/native/native_extract_by_attribute.h"
#include "algorithms/native/native_clip_raster.h"
#include "algorithms/native/native_raster_statistics.h"
#include "algorithms/native/native_hillshade.h"
#include "algorithms/native/native_reproject_layer.h"
#include "algorithms/native/native_assign_projection.h"

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
    SICNU_LOG_INFO( SicnuLogTags::Providers, QStringLiteral( "Loading QGIS algorithms provider" ) );
    int count = 0;

    // Raster algorithms
    addAlgorithm( new RasterCalculatorAlgorithm() ); ++count;
    addAlgorithm( new RasterResampleAlgorithm() ); ++count;
    addAlgorithm( new RasterClipAlgorithm() ); ++count;
    addAlgorithm( new RasterMergeBandsAlgorithm() ); ++count;
    addAlgorithm( new RasterNdviAlgorithm() ); ++count;
    addAlgorithm( new RasterStatisticsAlgorithm() ); ++count;

    // Vector algorithms
    addAlgorithm( new VectorBufferAlgorithm() ); ++count;
    addAlgorithm( new VectorClipAlgorithm() ); ++count;
    addAlgorithm( new VectorDissolveAlgorithm() ); ++count;
    addAlgorithm( new VectorMergeAlgorithm() ); ++count;
    addAlgorithm( new VectorSpatialQueryAlgorithm() ); ++count;
    addAlgorithm( new VectorAttributeQueryAlgorithm() ); ++count;
    addAlgorithm( new VectorReprojectAlgorithm() ); ++count;
    addAlgorithm( new VectorDifferenceAlgorithm() ); ++count;
    addAlgorithm( new VectorSymmetricalDifferenceAlgorithm() ); ++count;
    addAlgorithm( new VectorSelectByLocationAlgorithm() ); ++count;
    addAlgorithm( new VectorExtractByLocationAlgorithm() ); ++count;
    addAlgorithm( new VectorFieldCalculatorAlgorithm() ); ++count;
    addAlgorithm( new VectorNearestNeighborAlgorithm() ); ++count;
    addAlgorithm( new VectorDistanceMatrixAlgorithm() ); ++count;
    addAlgorithm( new VectorMultipartToSinglepartAlgorithm() ); ++count;
    addAlgorithm( new VectorSmoothGeometryAlgorithm() ); ++count;
    addAlgorithm( new VectorFixGeometriesAlgorithm() ); ++count;

    // Remote Sensing algorithms
    addAlgorithm( new BandMathAlgorithm() ); ++count;
    addAlgorithm( new SpectralIndexAlgorithm() ); ++count;
    addAlgorithm( new AtmosphericCorrectionAlgorithm() ); ++count;

    // Native algorithms (merged from sicnu_native)
    // Vector geometry — Buffer and Dissolve are provided by VectorBufferAlgorithm/VectorDissolveAlgorithm
    addAlgorithm( new QgsCentroidsAlgorithm() ); ++count;
    addAlgorithm( new QgsConvexHullAlgorithm() ); ++count;
    addAlgorithm( new QgsSimplifyAlgorithm() ); ++count;
    // Vector overlay
    addAlgorithm( new QgsClipAlgorithm() ); ++count;
    addAlgorithm( new QgsIntersectionAlgorithm() ); ++count;
    addAlgorithm( new QgsUnionAlgorithm() ); ++count;
    addAlgorithm( new QgsDifferenceAlgorithm() ); ++count;
    // Vector selection
    addAlgorithm( new QgsExtractByAttributeAlgorithm() ); ++count;
    // Raster analysis
    addAlgorithm( new QgsClipRasterByExtentAlgorithm() ); ++count;
    addAlgorithm( new QgsRasterLayerStatisticsAlgorithm() ); ++count;
    addAlgorithm( new QgsHillshadeAlgorithm() ); ++count;
    // Projection
    addAlgorithm( new QgsReprojectLayerAlgorithm() ); ++count;
    addAlgorithm( new QgsAssignProjectionAlgorithm() ); ++count;

    SICNU_LOG_INFO( SicnuLogTags::Providers, QString( "QGIS algorithms provider loaded: %1 algorithms" ).arg( count ) );
}
