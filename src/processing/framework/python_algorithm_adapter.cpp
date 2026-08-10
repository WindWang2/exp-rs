#include "python_algorithm_adapter.h"

#include <stdexcept>

namespace sicnu::processing {

PythonAlgorithmAdapter::PythonAlgorithmAdapter( AlgorithmDescriptor desc, ExecuteCallback executor )
  : mDesc( std::move( desc ) )
  , mExecutor( std::move( executor ) )
{
}

Json::Value PythonAlgorithmAdapter::execute( const Json::Value &params, ProgressCallback progressCb,
                                             std::function<bool()> isCancelledFn )
{
  if ( mExecutor )
  {
    if ( isCancelledFn && isCancelledFn() )
    {
      throw std::runtime_error( "Cancelled before execution" );
    }
    return mExecutor( params, progressCb, std::move( isCancelledFn ) );
  }
  throw std::runtime_error( "No execution handler provided for Python algorithm" );
}

} // namespace sicnu::processing
