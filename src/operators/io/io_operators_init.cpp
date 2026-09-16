/***************************************************************************
 * io_operators_init.cpp — Static registration of the io:* operator family.
 ***************************************************************************/
#include "io_fabric_operators.h"
#include "io_operators.h"
#include "operators/framework/rs_operator_registry.h"

namespace sicnu::operators::rs
{
/// Published by RSOperatorRegistry::instance() while its call_once chain
/// runs; defined in rs_operators_init.cpp.
extern RSOperatorRegistry *sRegistryUnderConstruction;
}

namespace sicnu::operators::io
{

REGISTER_RS_OPERATOR( IoTranslateOperator, "io:translate" )
REGISTER_RS_OPERATOR( IoWarpOperator, "io:warp" )
REGISTER_RS_OPERATOR( IoReprojectOperator, "io:reproject" )
REGISTER_RS_OPERATOR( IoClipOperator, "io:clip" )
REGISTER_RS_OPERATOR( IoConvertFormatOperator, "io:convert_format" )
REGISTER_RS_OPERATOR( IoBuildOverviewsOperator, "io:build_overviews" )
REGISTER_RS_OPERATOR( IoMakeCogOperator, "io:make_cog" )
REGISTER_RS_OPERATOR( IoVectorConvertOperator, "io:vector_convert" )
REGISTER_RS_OPERATOR( IoInspectOperator, "io:inspect" )
REGISTER_RS_OPERATOR( IoDoctorOperator, "io:doctor" )
REGISTER_RS_OPERATOR( IoSubdatasetsOperator, "io:subdatasets" )
REGISTER_RS_OPERATOR( IoMetadataPatchOperator, "io:metadata_patch" )
REGISTER_RS_OPERATOR( IoVerifyDatasetOperator, "io:verify_dataset" )

void initBuiltinIoOperators()
{
  // Runs inside RSOperatorRegistry::instance()'s call_once chain. The
  // REGISTER_RS_OPERATOR static initializers are dead-strippable on some
  // linkers, so this explicit list is the guaranteed registration path
  // (#707 — same rationale as the rs:/gdal: families). Idempotent.
  RSOperatorRegistry *registry = sicnu::operators::rs::sRegistryUnderConstruction;
  if ( !registry )
    return;
  const auto add = [ registry ]( const std::string &id, auto factory ) {
    registry->registerOperator( id, std::move( factory ) );
  };
  add( "io:translate", [] { return std::make_unique<IoTranslateOperator>(); } );
  add( "io:warp", [] { return std::make_unique<IoWarpOperator>(); } );
  add( "io:reproject", [] { return std::make_unique<IoReprojectOperator>(); } );
  add( "io:clip", [] { return std::make_unique<IoClipOperator>(); } );
  add( "io:convert_format", [] { return std::make_unique<IoConvertFormatOperator>(); } );
  add( "io:build_overviews", [] { return std::make_unique<IoBuildOverviewsOperator>(); } );
  add( "io:make_cog", [] { return std::make_unique<IoMakeCogOperator>(); } );
  add( "io:vector_convert", [] { return std::make_unique<IoVectorConvertOperator>(); } );
  add( "io:inspect", [] { return std::make_unique<IoInspectOperator>(); } );
  add( "io:doctor", [] { return std::make_unique<IoDoctorOperator>(); } );
  add( "io:catalog_search", [] { return std::make_unique<IoCatalogSearchOperator>(); } );
  add( "io:cube_plan", [] { return std::make_unique<IoCubePlanOperator>(); } );
  add( "io:cube_window", [] { return std::make_unique<IoCubeWindowOperator>(); } );
  add( "io:cache_prefetch", [] { return std::make_unique<IoCachePrefetchOperator>(); } );
  add( "io:subdatasets", [] { return std::make_unique<IoSubdatasetsOperator>(); } );
  add( "io:metadata_patch", [] { return std::make_unique<IoMetadataPatchOperator>(); } );
  add( "io:verify_dataset", [] { return std::make_unique<IoVerifyDatasetOperator>(); } );
}

} // namespace sicnu::operators::io
