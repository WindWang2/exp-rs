/***************************************************************************
 * opencv_operators_init.cpp  —  Static registration of OpenCV operators
 ***************************************************************************/
#include "opencv_filter_operators.h"
#include "operators/framework/rs_operator_registry.h"

#ifdef SICNU_HAS_OPENCV

namespace sicnu::operators::rs {
/// Published by RSOperatorRegistry::instance() while its call_once chain
/// runs; defined in rs_operators_init.cpp.
extern RSOperatorRegistry *sRegistryUnderConstruction;
}

namespace sicnu::operators::opencv {

REGISTER_RS_OPERATOR(OpenCvGaussianBlurOperator, "opencv:gaussian_blur")
REGISTER_RS_OPERATOR(OpenCvMeanBlurOperator, "opencv:mean_blur")
REGISTER_RS_OPERATOR(OpenCvMedianBlurOperator, "opencv:median_blur")
REGISTER_RS_OPERATOR(OpenCvSobelOperator, "opencv:sobel")
REGISTER_RS_OPERATOR(OpenCvLaplacianOperator, "opencv:laplacian")
REGISTER_RS_OPERATOR(OpenCvCannyOperator, "opencv:canny")

void initBuiltinOpenCvOperators() {
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
  add("opencv:gaussian_blur", [] { return std::make_unique<OpenCvGaussianBlurOperator>(); });
  add("opencv:mean_blur", [] { return std::make_unique<OpenCvMeanBlurOperator>(); });
  add("opencv:median_blur", [] { return std::make_unique<OpenCvMedianBlurOperator>(); });
  add("opencv:sobel", [] { return std::make_unique<OpenCvSobelOperator>(); });
  add("opencv:laplacian", [] { return std::make_unique<OpenCvLaplacianOperator>(); });
  add("opencv:canny", [] { return std::make_unique<OpenCvCannyOperator>(); });
}

} // namespace sicnu::operators::opencv

#endif // SICNU_HAS_OPENCV
