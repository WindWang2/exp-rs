#pragma once

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <QApplication>
#include <memory>

namespace exprs_test
{
// Construct Qt for the test run and release it before Qt's process-wide
// statics are destroyed. Catch2 discovery does not need a platform plugin.
class WidgetTestApplication final : public Catch::EventListenerBase
{
  public:
    using EventListenerBase::EventListenerBase;
    void testRunStarting( const Catch::TestRunInfo & ) override
    {
        application = std::make_unique<QApplication>( argc, argv );
    }
    void testRunEnded( const Catch::TestRunStats & ) override { application.reset(); }

  private:
    int argc = 1;
    char name[13] = "widget-tests";
    char *argv[2] = { name, nullptr };
    std::unique_ptr<QApplication> application;
};
}

CATCH_REGISTER_LISTENER( exprs_test::WidgetTestApplication )
