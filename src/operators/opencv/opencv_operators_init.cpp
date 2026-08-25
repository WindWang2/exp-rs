/***************************************************************************
 * opencv_operators_init.cpp  —  Static registration of OpenCV operators
 ***************************************************************************/
#include "opencv_filter_operators.h"
#include "operators/framework/rs_operator_registry.h"

#ifdef SICNU_HAS_OPENCV

namespace sicnu::operators::opencv {

REGISTER_RS_OPERATOR(OpenCvGaussianBlurOperator, "opencv:gaussian_blur")
REGISTER_RS_OPERATOR(OpenCvMeanBlurOperator, "opencv:mean_blur")
REGISTER_RS_OPERATOR(OpenCvMedianBlurOperator, "opencv:median_blur")
REGISTER_RS_OPERATOR(OpenCvSobelOperator, "opencv:sobel")
REGISTER_RS_OPERATOR(OpenCvLaplacianOperator, "opencv:laplacian")
REGISTER_RS_OPERATOR(OpenCvCannyOperator, "opencv:canny")

void initBuiltinOpenCvOperators() {}

} // namespace sicnu::operators::opencv

#endif // SICNU_HAS_OPENCV
