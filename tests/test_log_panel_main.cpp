// tests/test_log_panel_main.cpp — custom main for test_log_panel
//
// The QgsApplication must be destroyed before the process exits: destroying
// it during exit-time static destruction (e.g. a static std::unique_ptr)
// crashes inside QApplication::~QApplication -> qt_call_post_routines().
// Creating it on the stack here guarantees destruction at the end of main(),
// while all Qt/QGIS statics are still alive.
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>

#include <qgsapplication.h>

namespace {
int fakeArgc = 1;
char fakeArg0[] = "test_log_panel";
char *fakeArgv[] = {fakeArg0, nullptr};
} // namespace

int main(int argc, char *argv[])
{
    QgsApplication app(fakeArgc, fakeArgv, false);
    app.init();
    app.setPrefixPath(app.applicationDirPath(), true);

    Catch::Session session;
    const int rc = session.applyCommandLine(argc, argv);
    if (rc != 0)
        return rc;
    const int result = session.run();
#ifdef _WIN32
  _exit( result );
#else
  return result;
#endif
}
