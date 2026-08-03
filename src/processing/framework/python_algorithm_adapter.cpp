#include "python_algorithm_adapter.h"

#include <stdexcept>

namespace sicnu::processing {

PythonAlgorithmAdapter::PythonAlgorithmAdapter( AlgorithmDescriptor desc, ExecuteCallback executor )
  : mDesc( std::move( desc ) )
  , mExecutor( std::move( executor ) )
{
}

Json::Value PythonAlgorithmAdapter::execute( const Json::Value &params, ProgressCallback progressCb )
{
  if ( mExecutor )
  {
    return mExecutor( params, progressCb );
  }
  throw std::runtime_error( "No execution handler provided for Python algorithm" );
}

} // namespace sicnu::processing
