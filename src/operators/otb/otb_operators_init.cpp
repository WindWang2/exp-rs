/***************************************************************************
 * otb_operators_init.cpp  —  Static registration of OTB CLI operators
 ***************************************************************************/
#include "otb_compute_images_statistics_operator.h"
#include "otb_segmentation_operator.h"
#include "otb_svm_classification_operator.h"
#include "operators/framework/rs_operator_registry.h"

namespace sicnu::operators::otb {

REGISTER_RS_OPERATOR(OtbSegmentationOperator, "otb:meanshift_segmentation")
REGISTER_RS_OPERATOR(OtbSvmClassificationOperator, "otb:svm_classification")
REGISTER_RS_OPERATOR(OtbComputeImagesStatisticsOperator, "otb:compute_images_statistics")

void initBuiltinOtbOperators() {}

} // namespace sicnu::operators::otb
