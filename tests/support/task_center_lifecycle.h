#pragma once

#include "processing/framework/task_center.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace sicnu::test
{
class TaskCenterLifecycle final : public Catch::EventListenerBase
{
public:
    using EventListenerBase::EventListenerBase;

    void testRunEnded( const Catch::TestRunStats & ) override
    {
        // A terminal record is visible while its worker is still notifying
        // observers. Finish those callbacks before process-wide teardown.
        TaskCenter::instance().shutdown();
    }
};
}

// Include in one translation unit per Task Center test executable.
CATCH_REGISTER_LISTENER( sicnu::test::TaskCenterLifecycle )
