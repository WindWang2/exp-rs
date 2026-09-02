/***************************************************************************
 * gdal_operators_init.cpp  —  Static registration of GDAL-based RSOperators
 ***************************************************************************/
#include "gdal_orthorectification_operator.h"
#include "gdal_reproject_operator.h"
#include "gdal_clip_operator.h"
#include "gdal_polygonize_operator.h"
#include "operators/framework/rs_operator_registry.h"

namespace sicnu::operators::rs {
/// Published by RSOperatorRegistry::instance() while its call_once chain
/// runs; defined in rs_operators_init.cpp.
extern RSOperatorRegistry *sRegistryUnderConstruction;
}

namespace sicnu::operators::gdal {

REGISTER_RS_OPERATOR(GdalOrthorectificationOperator, "gdal:orthorectification")
REGISTER_RS_OPERATOR(GdalReprojectOperator, "gdal:reproject")
REGISTER_RS_OPERATOR(GdalClipOperator, "gdal:clip")
REGISTER_RS_OPERATOR(GdalPolygonizeOperator, "gdal:polygonize")

void initBuiltinGdalOperators() {
  // Runs inside RSOperatorRegistry::instance()'s call_once chain (the
  // registry under construction is published through
  // sRegistryUnderConstruction). The REGISTER_RS_OPERATOR static
  // initializers in this TU are dead-strippable when the linker decides no
  // symbol is referenced, so the explicit list below is the guaranteed
  // registration path (#707 — same rationale as the rs: family). The two
  // paths are idempotent: registerOperator overwrites the map entry.
  RSOperatorRegistry *registry = sicnu::operators::rs::sRegistryUnderConstruction;
  if (!registry)
    return;
  const auto add = [registry](const std::string &id, auto factory) {
    registry->registerOperator(id, std::move(factory));
  };
  add("gdal:orthorectification", [] { return std::make_unique<GdalOrthorectificationOperator>(); });
  add("gdal:reproject", [] { return std::make_unique<GdalReprojectOperator>(); });
  add("gdal:clip", [] { return std::make_unique<GdalClipOperator>(); });
  add("gdal:polygonize", [] { return std::make_unique<GdalPolygonizeOperator>(); });
}

} // namespace sicnu::operators::gdal
