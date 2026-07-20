/***************************************************************************
 * gdal_operators_init.cpp  —  Static registration of GDAL-based RSOperators
 ***************************************************************************/
#include "gdal_orthorectification_operator.h"
#include "gdal_reproject_operator.h"
#include "gdal_clip_operator.h"
#include "gdal_polygonize_operator.h"
#include "operators/framework/rs_operator_registry.h"

namespace sicnu::operators::gdal {

REGISTER_RS_OPERATOR(GdalOrthorectificationOperator, "gdal:orthorectification")
REGISTER_RS_OPERATOR(GdalReprojectOperator, "gdal:reproject")
REGISTER_RS_OPERATOR(GdalClipOperator, "gdal:clip")
REGISTER_RS_OPERATOR(GdalPolygonizeOperator, "gdal:polygonize")

} // namespace sicnu::operators::gdal
