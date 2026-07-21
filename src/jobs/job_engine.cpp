// job_engine.cpp
#include "job_engine.h"
namespace sicnu::jobs {
JobEngine &JobEngine::instance() {
  static JobEngine e;
  return e;
}
}
