/***************************************************************************
 * rs_operators_init.cpp  —  Static registration of native RS operators
 ***************************************************************************/
#include "rs_spectral_index_operator.h"
#include "rs_band_math_operator.h"
#include "rs_sam_classify_operator.h"
#include "rs_spectral_unmixing_operator.h"
#include "rs_rx_anomaly_operator.h"
#include "rs_continuum_removal_operator.h"
#include "rs_spectral_resample_operator.h"
#include "rs_endmember_extraction_operator.h"
#include "rs_atmospheric_correction_operator.h"
#include "rs_radiometric_calibration_operator.h"
#include "rs_change_detection_operator.h"
#include "rs_change_primitives.h"
#include "rs_threshold_raster_operator.h"
#include "rs_post_classification_change_operator.h"
#include "rs_qa_mask_operator.h"
#include "rs_apply_mask_operator.h"
#include "rs_image_fusion_operator.h"
#include "rs_fusion_aliases.h"
#include "rs_terrain_analysis_operator.h"
#include "rs_pca_operator.h"
#include "rs_mnf_operator.h"
#include "rs_mosaic_operator.h"
#include "rs_landsat_import_operator.h"
#include "rs_sentinel2_import_operator.h"
#include "rs_modis_import_operator.h"
#include "rs_modis_georeference_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"

#ifdef SICNU_HAS_OPENCV
#include "rs_kmeans_operator.h"
#include "rs_supervised_classification_operator.h"
#include "rs_obia_segment_operator.h"
#include "rs_obia_classify_operator.h"
#include "rs_obia_hierarchy_operator.h"
#include "rs_segment_stats_operator.h"
#include "rs_majority_filter_operator.h"
#include "rs_recode_operator.h"
#include "rs_inference_operator.h"
#endif

namespace sicnu::operators::rs {

REGISTER_RS_OPERATOR(RsSpectralIndexOperator, "rs:spectral_index")
REGISTER_RS_OPERATOR(RsBandMathOperator, "rs:band_math")
REGISTER_RS_OPERATOR(RsSamClassifyOperator, "rs:sam_classify")
REGISTER_RS_OPERATOR(RsSpectralUnmixingOperator, "rs:spectral_unmixing")
REGISTER_RS_OPERATOR(RsRxAnomalyOperator, "rs:rx_anomaly")
REGISTER_RS_OPERATOR(RsContinuumRemovalOperator, "rs:continuum_removal")
REGISTER_RS_OPERATOR(RsSpectralResampleOperator, "rs:spectral_resample")
REGISTER_RS_OPERATOR(RsEndmemberExtractionOperator, "rs:endmember_extraction")
REGISTER_RS_OPERATOR(RsAtmosphericCorrectionOperator, "rs:atmospheric_correction")
REGISTER_RS_OPERATOR(RsRadiometricCalibrationOperator, "rs:radiometric_calibration")
REGISTER_RS_OPERATOR(RsChangeDetectionOperator, "rs:change_detection")
REGISTER_RS_OPERATOR(RsChangeDifferenceOperator, "rs:change_difference")
REGISTER_RS_OPERATOR(RsChangeNormalizedDifferenceOperator, "rs:change_normalized_difference")
REGISTER_RS_OPERATOR(RsChangeRatioOperator, "rs:change_ratio")
REGISTER_RS_OPERATOR(RsChangeCvaOperator, "rs:change_cva")
REGISTER_RS_OPERATOR(RsChangeMadOperator, "rs:change_mad")
REGISTER_RS_OPERATOR(RsThresholdRasterOperator, "rs:threshold_raster")
REGISTER_RS_OPERATOR(RsPostClassificationChangeOperator, "rs:post_classification_change")
REGISTER_RS_OPERATOR(RsQaMaskOperator, "rs:qa_mask")
REGISTER_RS_OPERATOR(RsApplyMaskOperator, "rs:apply_mask")
REGISTER_RS_OPERATOR(RsImageFusionOperator, "rs:image_fusion")
REGISTER_RS_OPERATOR(RsFusionLinearOperator, "rs:fusion_linear")
REGISTER_RS_OPERATOR(RsFusionBroveyOperator, "rs:fusion_brovey")
REGISTER_RS_OPERATOR(RsFusionPcaOperator, "rs:fusion_pca")
REGISTER_RS_OPERATOR(RsFusionIhsOperator, "rs:fusion_ihs")
REGISTER_RS_OPERATOR(RsFusionGramSchmidtOperator, "rs:fusion_gram_schmidt")
REGISTER_RS_OPERATOR(RsTerrainAnalysisOperator, "rs:terrain_analysis")
REGISTER_RS_OPERATOR(RsPcaOperator, "rs:pca")
REGISTER_RS_OPERATOR(RsMnfOperator, "rs:mnf")
REGISTER_RS_OPERATOR(RsMosaicOperator, "rs:mosaic")
REGISTER_RS_OPERATOR(RsLandsatImportOperator, "rs:landsat_import")
REGISTER_RS_OPERATOR(RsSentinel2ImportOperator, "rs:sentinel2_import")
REGISTER_RS_OPERATOR(RsModisImportOperator, "rs:modis_import")
REGISTER_RS_OPERATOR(RsModisGeoreferenceOperator, "rs:modis_georeference")

#ifdef SICNU_HAS_OPENCV
REGISTER_RS_OPERATOR(RsKmeansOperator, "rs:kmeans_classification")
REGISTER_RS_OPERATOR(RsSupervisedClassificationOperator, "rs:supervised_classification")
REGISTER_RS_OPERATOR(RsObiaSegmentOperator, "rs:obia_segment")
REGISTER_RS_OPERATOR(RsObiaClassifyOperator, "rs:obia_classify")
REGISTER_RS_OPERATOR(RsObiaHierarchyOperator, "rs:obia_hierarchy")
REGISTER_RS_OPERATOR(RsSegmentStatsOperator, "rs:segment_stats")
REGISTER_RS_OPERATOR(RsMajorityFilterOperator, "rs:majority_filter")
REGISTER_RS_OPERATOR(RsRecodeOperator, "rs:recode")
REGISTER_RS_OPERATOR(RsInferenceOperator, "rs:infer")
#endif

struct AtomicRsOperatorProviderRegistration {
  AtomicRsOperatorProviderRegistration() {
    sicnu::processing::AtomicAlgorithmRegistry::setRsOperatorProvider([](sicnu::processing::AtomicAlgorithmRegistry &registry) {
      auto names = RSOperatorRegistry::instance().operatorNames();
      for ( const auto &name : names ) {
        auto op = RSOperatorRegistry::instance().create( name );
        if ( op ) {
          auto adapter = std::make_shared<sicnu::processing::RsOperatorAdapter>( std::move( op ) );
          registry.registerAdapter( adapter );
        }
      }
    });
  }
};
static AtomicRsOperatorProviderRegistration sAtomicRsOperatorProviderReg;

} // namespace sicnu::operators::rs
