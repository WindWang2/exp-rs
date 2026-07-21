// src/workflow/workflow_runner.cpp
#include "workflow_runner.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <stdexcept>

namespace sicnu::workflow {

Json::Value WorkflowRunner::run( const std::string &operatorId, const Json::Value &params )
{
  auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId );
  if ( !op )
  {
    throw std::runtime_error( "Operator not found: " + operatorId );
  }

  sicnu::operators::RSOperatorContext context;
  try
  {
    return op->execute( params, context );
  }
  catch ( const sicnu::operators::RSOperatorError &e )
  {
    throw std::runtime_error( e.message() );
  }
}

} // namespace sicnu::workflow
