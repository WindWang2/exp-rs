// tests/test_raster_processing_dialog_base_main.cpp — custom main
//
// The QApplication must be destroyed before the process exits. The previous
// pattern (leaking a `new QApplication` in ensureApp()) left pending posted
// events (task-thread deferred deletes, style animations) in the dispatcher
// queue; at process exit libQt6Core's __cxa_finalize delivered them to
// already-destroyed objects (breeze6 style) and crashed. A stack-allocated
// app destroyed at the end of main() flushes that state while Qt statics are
// still alive.
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>

#include <QApplication>

namespace {
int fakeArgc = 1;
char fakeArg0[] = "test_raster_processing_dialog_base";
char *fakeArgv[] = {fakeArg0, nullptr};
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(fakeArgc, fakeArgv);

    Catch::Session session;
    const int rc = session.applyCommandLine(argc, argv);
    if (rc != 0)
        return rc;
    return session.run();
}
