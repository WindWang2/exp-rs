// src/processing/providers/otb_tools/provider.cpp
#include "provider.h"
#include "processing/providers/generic_cli/generic_cli_algorithm.h"
#include "processing/tools/cli_tool_discovery.h"
#include "algorithms/otb_band_math.h"
#include "algorithms/otb_segmentation.h"
#include "algorithms/otb_extract_roi.h"
#include "algorithms/otb_concatenate_images.h"
#include "algorithms/otb_dynamic_convert.h"
#include "algorithms/otb_rescale.h"
#include "algorithms/otb_convert.h"
#include "algorithms/otb_mean_shift_smoothing.h"
#include "algorithms/otb_lsms.h"
#include "algorithms/otb_train_vector_classifier.h"
#include "algorithms/otb_image_classifier.h"
#include "algorithms/otb_kmeans_classification.h"
#include "algorithms/otb_feature_extraction.h"
#include "algorithms/otb_haralick_texture.h"
#include "algorithms/otb_radiometric_indices.h"
#include "algorithms/otb_ortho_rectification.h"
#include "algorithms/otb_bundle_to_perfect_sensor.h"
#include "algorithms/otb_superimpose.h"
#include "algorithms/otb_binary_morphological.h"
#include "algorithms/otb_band_math_x.h"
#include "algorithms/otb_compute_images_statistics.h"
#include "algorithms/otb_multi_resolution_pyramid.h"
#include "algorithms/otb_gray_scale_morphological.h"
#include "algorithms/otb_pixel_info.h"
#include "algorithms/otb_read_image_info.h"
#include "algorithms/otb_gray_level_cooccurrence_matrix.h"
#include "algorithms/otb_local_statistic_extraction.h"
#include "algorithms/otb_multivariate_alteration_detector.h"
#include "algorithms/otb_svm_classification.h"
#include "algorithms/otb_stereo_rectification.h"

#include <QIcon>

OtbToolsProvider::OtbToolsProvider()
    : QgsProcessingProvider()
{
}

QIcon OtbToolsProvider::icon() const
{
    return QIcon::fromTheme("otb");
}

void OtbToolsProvider::loadAlgorithms()
{
    // Radiometry
    addAlgorithm(new OtbBandMathAlgorithm());

    // Segmentation
    addAlgorithm(new OtbSegmentationAlgorithm());

    // Utilities
    addAlgorithm(new OtbExtractRoiAlgorithm());
    addAlgorithm(new OtbConcatenateImagesAlgorithm());
    addAlgorithm(new OtbDynamicConvertAlgorithm());
    addAlgorithm(new OtbRescaleAlgorithm());
    addAlgorithm(new OtbConvertAlgorithm());

    // Filtering
    addAlgorithm(new OtbMeanShiftSmoothingAlgorithm());
    addAlgorithm(new OtbLsmsAlgorithm());

    // Learning
    addAlgorithm(new OtbTrainVectorClassifierAlgorithm());
    addAlgorithm(new OtbImageClassifierAlgorithm());
    addAlgorithm(new OtbKMeansClassificationAlgorithm());
    addAlgorithm(new OtbSvmClassificationAlgorithm());

    // Feature
    addAlgorithm(new OtbFeatureExtractionAlgorithm());
    addAlgorithm(new OtbHaralickTextureAlgorithm());
    addAlgorithm(new OtbGrayLevelCooccurrenceMatrixAlgorithm());
    addAlgorithm(new OtbLocalStatisticExtractionAlgorithm());
    addAlgorithm(new OtbRadiometricIndicesAlgorithm());

    // Geometry
    addAlgorithm(new OtbOrthoRectificationAlgorithm());
    addAlgorithm(new OtbBundleToPerfectSensorAlgorithm());
    addAlgorithm(new OtbSuperimposeAlgorithm());
    addAlgorithm(new OtbStereoRectificationAlgorithm());
    addAlgorithm(new OtbMultivariateAlterationDetectorAlgorithm());

    // Image Processing
    addAlgorithm(new OtbBinaryMorphologicalAlgorithm());
    addAlgorithm(new OtbGrayScaleMorphologicalAlgorithm());
    addAlgorithm(new OtbMultiResolutionPyramidAlgorithm());

    // Advanced Radiometry
    addAlgorithm(new OtbBandMathXAlgorithm());
    addAlgorithm(new OtbComputeImagesStatisticsAlgorithm());

    // Info Tools
    addAlgorithm(new OtbPixelInfoAlgorithm());
    addAlgorithm(new OtbReadImageInfoAlgorithm());

    // Auto-discovered OTB applications not covered by handcrafted wrappers
    for ( const QString &appName : CliToolDiscovery::discoverOtbApplicationNames() )
    {
        addAlgorithm( new GenericCliAlgorithm(
            CliToolDiscovery::makeOtbDiscoveredConfig( appName ), id() ) );
    }
}
