/***************************************************************************
 * otb_operators_init.cpp  —  Static registration of OTB CLI operators
 ***************************************************************************/
#include "otb_compute_images_statistics_operator.h"
#include "otb_segmentation_operator.h"
#include "otb_svm_classification_operator.h"
#include "operators/framework/rs_operator_registry.h"

namespace sicnu::operators::rs {
/// Published by RSOperatorRegistry::instance() while its call_once chain
/// runs; defined in rs_operators_init.cpp.
extern RSOperatorRegistry *sRegistryUnderConstruction;
}

namespace sicnu::operators::otb {

REGISTER_RS_OPERATOR(OtbSegmentationOperator, "otb:meanshift_segmentation")
REGISTER_RS_OPERATOR(OtbSvmClassificationOperator, "otb:svm_classification")
REGISTER_RS_OPERATOR(OtbComputeImagesStatisticsOperator, "otb:compute_images_statistics")

void initBuiltinOtbOperators() {
  // Runs inside RSOperatorRegistry::instance()'s call_once chain. The
  // REGISTER_RS_OPERATOR static initializers in this TU are dead-strippable
  // when the linker decides no symbol is referenced, so the explicit list
  // below is the guaranteed registration path (#707 — same rationale as the
  // rs: family). The two paths are idempotent.
  RSOperatorRegistry *registry = sicnu::operators::rs::sRegistryUnderConstruction;
  if (!registry)
    return;
  const auto add = [registry](const std::string &id, auto factory) {
    registry->registerOperator(id, std::move(factory));
  };
  add("otb:meanshift_segmentation", [] { return std::make_unique<OtbSegmentationOperator>(); });
  add("otb:svm_classification", [] { return std::make_unique<OtbSvmClassificationOperator>(); });
  add("otb:compute_images_statistics", [] { return std::make_unique<OtbComputeImagesStatisticsOperator>(); });
}

} // namespace sicnu::operators::otb
