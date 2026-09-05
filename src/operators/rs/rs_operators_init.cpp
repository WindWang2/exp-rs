/***************************************************************************
 * rs_operators_init.cpp  —  Static registration of native RS operators
 ***************************************************************************/
#include "rs_spectral_index_operator.h"
#include "rs_spectral_index_aliases.h"
#include "rs_band_math_operator.h"
#include "rs_sam_classify_operator.h"
#include "rs_spectral_unmixing_operator.h"
#include "rs_rx_anomaly_operator.h"
#include "rs_continuum_removal_operator.h"
#include "rs_spectral_resample_operator.h"
#include "rs_endmember_extraction_operator.h"
#include "rs_atmospheric_correction_operator.h"
#include "rs_atmospheric_aliases.h"
#include "rs_radiometric_calibration_operator.h"
#include "rs_change_detection_operator.h"
#include "rs_change_primitives.h"
#include "rs_threshold_raster_operator.h"
#include "rs_sar_calibrate_operator.h"
#include "rs_sar_backscatter_operator.h"
#include "rs_sar_change_operator.h"
#include "rs_sar_ratio_operator.h"
#include "rs_sar_speckle_operator.h"
#include "rs_sar_terrain_correction_operator.h"
#include "rs_sar_terrain_flatten_operator.h"
#include "rs_sar_texture_operator.h"
#include "rs_post_classification_change_operator.h"
#include "rs_qa_mask_operator.h"
#include "rs_apply_mask_operator.h"
#include "rs_image_fusion_operator.h"
#include "rs_fusion_aliases.h"
#include "rs_feature_stack_operator.h"
#include "rs_feature_normalize_operator.h"
#include "rs_feature_select_operator.h"
#include "rs_terrain_analysis_operator.h"
#include "rs_pca_operator.h"
#include "rs_mnf_operator.h"
#include "rs_mosaic_operator.h"
#include "rs_temporal_summary_operator.h"
#include "rs_temporal_composite_operator.h"
#include "rs_temporal_index_operator.h"
#include "rs_temporal_trend_operator.h"
#include "rs_temporal_anomaly_operator.h"
#include "rs_temporal_breakpoints_operator.h"
#include "rs_temporal_decompose_operator.h"
#include "rs_temporal_gap_fill_operator.h"
#include "rs_temporal_harmonic_fit_operator.h"
#include "rs_temporal_phenology_operator.h"
#include "rs_temporal_smooth_operator.h"
#include "rs_temporal_extract_series_operator.h"
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
#include "rs_obia_features_operator.h"
#include "rs_obia_label_operator.h"
#include "rs_segment_stats_operator.h"
#include "rs_majority_filter_operator.h"
#include "rs_recode_operator.h"
#include "rs_inference_operator.h"
#endif

namespace sicnu::operators::rs {

REGISTER_RS_OPERATOR(RsSpectralIndexOperator, "rs:spectral_index")
REGISTER_RS_OPERATOR(RsNdviOperator, "rs:ndvi")
REGISTER_RS_OPERATOR(RsEviOperator, "rs:evi")
REGISTER_RS_OPERATOR(RsNdwiOperator, "rs:ndwi")
REGISTER_RS_OPERATOR(RsSaviOperator, "rs:savi")
REGISTER_RS_OPERATOR(RsNdbiOperator, "rs:ndbi")
REGISTER_RS_OPERATOR(RsMndwiOperator, "rs:mndwi")
REGISTER_RS_OPERATOR(RsBandMathOperator, "rs:band_math")
REGISTER_RS_OPERATOR(RsSamClassifyOperator, "rs:sam_classify")
REGISTER_RS_OPERATOR(RsSpectralUnmixingOperator, "rs:spectral_unmixing")
REGISTER_RS_OPERATOR(RsRxAnomalyOperator, "rs:rx_anomaly")
REGISTER_RS_OPERATOR(RsContinuumRemovalOperator, "rs:continuum_removal")
REGISTER_RS_OPERATOR(RsSpectralResampleOperator, "rs:spectral_resample")
REGISTER_RS_OPERATOR(RsEndmemberExtractionOperator, "rs:endmember_extraction")
REGISTER_RS_OPERATOR(RsAtmosphericCorrectionOperator, "rs:atmospheric_correction")
REGISTER_RS_OPERATOR(RsDnToRadianceOperator, "rs:dn_to_radiance")
REGISTER_RS_OPERATOR(RsAtmosphericDos1Operator, "rs:atmospheric_dos1")
REGISTER_RS_OPERATOR(RsAtmosphericDos2Operator, "rs:atmospheric_dos2")
REGISTER_RS_OPERATOR(RsAtmosphericQuacOperator, "rs:atmospheric_quac")
REGISTER_RS_OPERATOR(RsRadiometricCalibrationOperator, "rs:radiometric_calibration")
REGISTER_RS_OPERATOR(RsChangeDetectionOperator, "rs:change_detection")
REGISTER_RS_OPERATOR(RsChangeDifferenceOperator, "rs:change_difference")
REGISTER_RS_OPERATOR(RsChangeNormalizedDifferenceOperator, "rs:change_normalized_difference")
REGISTER_RS_OPERATOR(RsChangeRatioOperator, "rs:change_ratio")
REGISTER_RS_OPERATOR(RsChangeCvaOperator, "rs:change_cva")
REGISTER_RS_OPERATOR(RsChangeCvaAngleOperator, "rs:change_cva_angle")
REGISTER_RS_OPERATOR(RsChangeSamOperator, "rs:change_sam")
REGISTER_RS_OPERATOR(RsChangeLogRatioOperator, "rs:change_log_ratio")
REGISTER_RS_OPERATOR(RsChangeMadOperator, "rs:change_mad")
REGISTER_RS_OPERATOR(RsChangeIrMadOperator, "rs:change_irmad")
REGISTER_RS_OPERATOR(RsThresholdRasterOperator, "rs:threshold_raster")
REGISTER_RS_OPERATOR(RsSarCalibrateOperator, "rs:sar_calibrate")
REGISTER_RS_OPERATOR(RsSarBackscatterOperator, "rs:sar_backscatter")
REGISTER_RS_OPERATOR(RsSarTerrainFlattenOperator, "rs:sar_terrain_flatten")
REGISTER_RS_OPERATOR(RsSarTerrainCorrectionOperator, "rs:sar_terrain_correction")
REGISTER_RS_OPERATOR(RsSarSpeckleOperator, "rs:sar_speckle")
REGISTER_RS_OPERATOR(RsSarRatioOperator, "rs:sar_ratio")
REGISTER_RS_OPERATOR(RsSarTextureOperator, "rs:sar_texture")
REGISTER_RS_OPERATOR(RsSarChangeOperator, "rs:sar_change")
REGISTER_RS_OPERATOR(RsPostClassificationChangeOperator, "rs:post_classification_change")
REGISTER_RS_OPERATOR(RsQaMaskOperator, "rs:qa_mask")
REGISTER_RS_OPERATOR(RsApplyMaskOperator, "rs:apply_mask")
REGISTER_RS_OPERATOR(RsImageFusionOperator, "rs:image_fusion")
REGISTER_RS_OPERATOR(RsFusionLinearOperator, "rs:fusion_linear")
REGISTER_RS_OPERATOR(RsFusionBroveyOperator, "rs:fusion_brovey")
REGISTER_RS_OPERATOR(RsFusionPcaOperator, "rs:fusion_pca")
REGISTER_RS_OPERATOR(RsFusionIhsOperator, "rs:fusion_ihs")
REGISTER_RS_OPERATOR(RsFusionGramSchmidtOperator, "rs:fusion_gram_schmidt")
REGISTER_RS_OPERATOR(RsFeatureStackOperator, "rs:feature_stack")
REGISTER_RS_OPERATOR(RsFeatureNormalizeOperator, "rs:feature_normalize")
REGISTER_RS_OPERATOR(RsFeatureSelectOperator, "rs:feature_select")
REGISTER_RS_OPERATOR(RsTerrainAnalysisOperator, "rs:terrain_analysis")
REGISTER_RS_OPERATOR(RsPcaOperator, "rs:pca")
REGISTER_RS_OPERATOR(RsMnfOperator, "rs:mnf")
REGISTER_RS_OPERATOR(RsMosaicOperator, "rs:mosaic")
REGISTER_RS_OPERATOR(RsTemporalSummaryOperator, "rs:temporal_summary")
REGISTER_RS_OPERATOR(RsTemporalCompositeOperator, "rs:temporal_composite")
REGISTER_RS_OPERATOR(RsTemporalIndexSeriesOperator, "rs:temporal_index_series")
REGISTER_RS_OPERATOR(RsTemporalTrendOperator, "rs:temporal_trend")
REGISTER_RS_OPERATOR(RsTemporalSmoothOperator, "rs:temporal_smooth")
REGISTER_RS_OPERATOR(RsTemporalGapFillOperator, "rs:temporal_gap_fill")
REGISTER_RS_OPERATOR(RsTemporalHarmonicFitOperator, "rs:temporal_harmonic_fit")
REGISTER_RS_OPERATOR(RsTemporalPhenologyOperator, "rs:temporal_phenology")
REGISTER_RS_OPERATOR(RsTemporalBreakpointsOperator, "rs:temporal_breakpoints")
REGISTER_RS_OPERATOR(RsTemporalDecomposeOperator, "rs:temporal_decompose")
REGISTER_RS_OPERATOR(RsTemporalAnomalyOperator, "rs:temporal_anomaly")
REGISTER_RS_OPERATOR(RsTemporalExtractSeriesOperator, "rs:temporal_extract_series")
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
REGISTER_RS_OPERATOR(RsObiaFeaturesOperator, "rs:obia_features")
REGISTER_RS_OPERATOR(RsObiaLabelOperator, "rs:obia_label")
REGISTER_RS_OPERATOR(RsSegmentStatsOperator, "rs:segment_stats")
REGISTER_RS_OPERATOR(RsMajorityFilterOperator, "rs:majority_filter")
REGISTER_RS_OPERATOR(RsRecodeOperator, "rs:recode")
REGISTER_RS_OPERATOR(RsInferenceOperator, "rs:infer")
#endif

void installRsOperatorProvider();
void markRegistryInitComplete();

struct AtomicRsOperatorProviderRegistration {
  AtomicRsOperatorProviderRegistration() {
    installRsOperatorProvider();
  }
};
static AtomicRsOperatorProviderRegistration sAtomicRsOperatorProviderReg;

RSOperatorRegistry *sRegistryUnderConstruction = nullptr;

void initBuiltinRsOperators() {
  // Runs inside RSOperatorRegistry::instance()'s call_once chain. The
  // registry object is being constructed; its address is published through
  // sRegistryUnderConstruction so this function never re-enters instance()
  // (doing so from inside the chain leaves the init guard unreleased and
  // the next instance() re-runs the chain, clearing m_factories — #707).
  RSOperatorRegistry *registry = sRegistryUnderConstruction;
  if ( !registry )
    return;

  // The REGISTER_RS_OPERATOR static initializers in this TU are
  // dead-stripped when the linker decides no symbol is referenced, so the
  // explicit list below is the ONLY guaranteed registration path (#707).
  const auto add = [registry]( const std::string &id, auto factory ) {
    registry->registerOperator( id, std::move( factory ) );
  };
  add( "rs:spectral_index", [] { return std::make_unique<RsSpectralIndexOperator>(); } );
  add( "rs:ndvi", [] { return std::make_unique<RsNdviOperator>(); } );
  add( "rs:evi", [] { return std::make_unique<RsEviOperator>(); } );
  add( "rs:ndwi", [] { return std::make_unique<RsNdwiOperator>(); } );
  add( "rs:savi", [] { return std::make_unique<RsSaviOperator>(); } );
  add( "rs:ndbi", [] { return std::make_unique<RsNdbiOperator>(); } );
  add( "rs:mndwi", [] { return std::make_unique<RsMndwiOperator>(); } );
  add( "rs:band_math", [] { return std::make_unique<RsBandMathOperator>(); } );
  add( "rs:sam_classify", [] { return std::make_unique<RsSamClassifyOperator>(); } );
  add( "rs:spectral_unmixing", [] { return std::make_unique<RsSpectralUnmixingOperator>(); } );
  add( "rs:rx_anomaly", [] { return std::make_unique<RsRxAnomalyOperator>(); } );
  add( "rs:continuum_removal", [] { return std::make_unique<RsContinuumRemovalOperator>(); } );
  add( "rs:spectral_resample", [] { return std::make_unique<RsSpectralResampleOperator>(); } );
  add( "rs:endmember_extraction", [] { return std::make_unique<RsEndmemberExtractionOperator>(); } );
  add( "rs:atmospheric_correction", [] { return std::make_unique<RsAtmosphericCorrectionOperator>(); } );
  add( "rs:dn_to_radiance", [] { return std::make_unique<RsDnToRadianceOperator>(); } );
  add( "rs:atmospheric_dos1", [] { return std::make_unique<RsAtmosphericDos1Operator>(); } );
  add( "rs:atmospheric_dos2", [] { return std::make_unique<RsAtmosphericDos2Operator>(); } );
  add( "rs:atmospheric_quac", [] { return std::make_unique<RsAtmosphericQuacOperator>(); } );
  add( "rs:radiometric_calibration", [] { return std::make_unique<RsRadiometricCalibrationOperator>(); } );
  add( "rs:change_detection", [] { return std::make_unique<RsChangeDetectionOperator>(); } );
  add( "rs:change_difference", [] { return std::make_unique<RsChangeDifferenceOperator>(); } );
  add( "rs:change_normalized_difference", [] { return std::make_unique<RsChangeNormalizedDifferenceOperator>(); } );
  add( "rs:change_ratio", [] { return std::make_unique<RsChangeRatioOperator>(); } );
  add( "rs:change_cva", [] { return std::make_unique<RsChangeCvaOperator>(); } );
  add( "rs:change_cva_angle", [] { return std::make_unique<RsChangeCvaAngleOperator>(); } );
  add( "rs:change_sam", [] { return std::make_unique<RsChangeSamOperator>(); } );
  add( "rs:change_log_ratio", [] { return std::make_unique<RsChangeLogRatioOperator>(); } );
  add( "rs:change_mad", [] { return std::make_unique<RsChangeMadOperator>(); } );
  add( "rs:change_irmad", [] { return std::make_unique<RsChangeIrMadOperator>(); } );
  add( "rs:threshold_raster", [] { return std::make_unique<RsThresholdRasterOperator>(); } );
  add( "rs:sar_calibrate", [] { return std::make_unique<RsSarCalibrateOperator>(); } );
  add( "rs:sar_backscatter", [] { return std::make_unique<RsSarBackscatterOperator>(); } );
  add( "rs:sar_terrain_flatten", [] { return std::make_unique<RsSarTerrainFlattenOperator>(); } );
  add( "rs:sar_terrain_correction", [] { return std::make_unique<RsSarTerrainCorrectionOperator>(); } );
  add( "rs:sar_speckle", [] { return std::make_unique<RsSarSpeckleOperator>(); } );
  add( "rs:sar_ratio", [] { return std::make_unique<RsSarRatioOperator>(); } );
  add( "rs:sar_texture", [] { return std::make_unique<RsSarTextureOperator>(); } );
  add( "rs:sar_change", [] { return std::make_unique<RsSarChangeOperator>(); } );
  add( "rs:post_classification_change", [] { return std::make_unique<RsPostClassificationChangeOperator>(); } );
  add( "rs:qa_mask", [] { return std::make_unique<RsQaMaskOperator>(); } );
  add( "rs:apply_mask", [] { return std::make_unique<RsApplyMaskOperator>(); } );
  add( "rs:image_fusion", [] { return std::make_unique<RsImageFusionOperator>(); } );
  add( "rs:fusion_linear", [] { return std::make_unique<RsFusionLinearOperator>(); } );
  add( "rs:fusion_brovey", [] { return std::make_unique<RsFusionBroveyOperator>(); } );
  add( "rs:fusion_pca", [] { return std::make_unique<RsFusionPcaOperator>(); } );
  add( "rs:fusion_ihs", [] { return std::make_unique<RsFusionIhsOperator>(); } );
  add( "rs:fusion_gram_schmidt", [] { return std::make_unique<RsFusionGramSchmidtOperator>(); } );
  add( "rs:feature_stack", [] { return std::make_unique<RsFeatureStackOperator>(); } );
  add( "rs:feature_normalize", [] { return std::make_unique<RsFeatureNormalizeOperator>(); } );
  add( "rs:feature_select", [] { return std::make_unique<RsFeatureSelectOperator>(); } );
  add( "rs:terrain_analysis", [] { return std::make_unique<RsTerrainAnalysisOperator>(); } );
  add( "rs:pca", [] { return std::make_unique<RsPcaOperator>(); } );
  add( "rs:mnf", [] { return std::make_unique<RsMnfOperator>(); } );
  add( "rs:mosaic", [] { return std::make_unique<RsMosaicOperator>(); } );
  add( "rs:temporal_summary", [] { return std::make_unique<RsTemporalSummaryOperator>(); } );
  add( "rs:temporal_composite", [] { return std::make_unique<RsTemporalCompositeOperator>(); } );
  add( "rs:temporal_index_series", [] { return std::make_unique<RsTemporalIndexSeriesOperator>(); } );
  add( "rs:temporal_trend", [] { return std::make_unique<RsTemporalTrendOperator>(); } );
  add( "rs:temporal_smooth", [] { return std::make_unique<RsTemporalSmoothOperator>(); } );
  add( "rs:temporal_gap_fill", [] { return std::make_unique<RsTemporalGapFillOperator>(); } );
  add( "rs:temporal_harmonic_fit", [] { return std::make_unique<RsTemporalHarmonicFitOperator>(); } );
  add( "rs:temporal_phenology", [] { return std::make_unique<RsTemporalPhenologyOperator>(); } );
  add( "rs:temporal_breakpoints", [] { return std::make_unique<RsTemporalBreakpointsOperator>(); } );
  add( "rs:temporal_decompose", [] { return std::make_unique<RsTemporalDecomposeOperator>(); } );
  add( "rs:temporal_anomaly", [] { return std::make_unique<RsTemporalAnomalyOperator>(); } );
  add( "rs:temporal_extract_series", [] { return std::make_unique<RsTemporalExtractSeriesOperator>(); } );
  add( "rs:landsat_import", [] { return std::make_unique<RsLandsatImportOperator>(); } );
  add( "rs:sentinel2_import", [] { return std::make_unique<RsSentinel2ImportOperator>(); } );
  add( "rs:modis_import", [] { return std::make_unique<RsModisImportOperator>(); } );
  add( "rs:modis_georeference", [] { return std::make_unique<RsModisGeoreferenceOperator>(); } );
#ifdef SICNU_HAS_OPENCV
  add( "rs:kmeans_classification", [] { return std::make_unique<RsKmeansOperator>(); } );
  add( "rs:supervised_classification", [] { return std::make_unique<RsSupervisedClassificationOperator>(); } );
  add( "rs:obia_segment", [] { return std::make_unique<RsObiaSegmentOperator>(); } );
  add( "rs:obia_classify", [] { return std::make_unique<RsObiaClassifyOperator>(); } );
  add( "rs:obia_hierarchy", [] { return std::make_unique<RsObiaHierarchyOperator>(); } );
  add( "rs:obia_features", [] { return std::make_unique<RsObiaFeaturesOperator>(); } );
  add( "rs:obia_label", [] { return std::make_unique<RsObiaLabelOperator>(); } );
  add( "rs:segment_stats", [] { return std::make_unique<RsSegmentStatsOperator>(); } );
  add( "rs:majority_filter", [] { return std::make_unique<RsMajorityFilterOperator>(); } );
  add( "rs:recode", [] { return std::make_unique<RsRecodeOperator>(); } );
  add( "rs:infer", [] { return std::make_unique<RsInferenceOperator>(); } );
#endif
}

void installRsOperatorProvider() {
  // Safe to call from static init or after startup: if this is the first
  // instance() call it simply RUNS the call_once chain (constructing the
  // registry); what must never happen is calling instance() from INSIDE
  // that chain, which would re-enter the same call_once and deadlock
  // (#707). Capture the registry by reference: the provider callback runs
  // from AtomicAlgorithmRegistry::initialize()/reset().
  auto *rsRegistry = &RSOperatorRegistry::instance();
  sicnu::processing::AtomicAlgorithmRegistry::setRsOperatorProvider([rsRegistry](sicnu::processing::AtomicAlgorithmRegistry &registry) {
    auto names = rsRegistry->operatorNames();
    for ( const auto &name : names ) {
      auto op = rsRegistry->create( name );
      if ( op ) {
        auto adapter = std::make_shared<sicnu::processing::RsOperatorAdapter>( std::move( op ) );
        registry.registerAdapter( adapter );
      }
    }
  });
}

} // namespace sicnu::operators::rs
