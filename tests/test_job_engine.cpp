#include <catch2/catch_test_macros.hpp>
#include "jobs/job_types.h"

TEST_CASE( "job types compile", "[job]" )
{
  sicnu::jobs::JobRequest r;
  r.algorithmId = "test:noop";
  REQUIRE( r.algorithmId == "test:noop" );
}
