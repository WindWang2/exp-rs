#include "framework/atomic_algorithm_registry.h"
#include "framework/task_center.h"

namespace sicnu::processing {

long AtomicAlgorithmRegistry::submitToolCall( const std::string &jsonToolCall, bool autoLoad )
{
  return sicnu::TaskCenter::instance().enqueueToolCall( jsonToolCall, autoLoad );
}

} // namespace sicnu::processing
