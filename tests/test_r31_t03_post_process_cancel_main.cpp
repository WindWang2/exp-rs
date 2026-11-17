// test_r31_t03_post_process_cancel_main.cpp — stack-owned QGIS application.
//
// QgsTask/QgsApplication::messageLog() needs a live QgsApplication. Creating it
// on the stack here (rather than a leaked static) keeps destruction ordered and
// makes PRE_TEST discovery deterministic with the offscreen Qt platform.
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>

#include <qgsapplication.h>
#include <gdal.h>

namespace
{
int fakeArgc = 1;
char fakeArg0[] = "test_r31_t03_post_process_cancel";
char *fakeArgv[] = { fakeArg0, nullptr };
} // namespace

int main( int argc, char *argv[] )
{
  QgsApplication app( fakeArgc, fakeArgv, false );
  app.init();
  app.setPrefixPath( app.applicationDirPath(), true );
  GDALAllRegister();

  Catch::Session session;
  const int rc = session.applyCommandLine( argc, argv );
  if ( rc != 0 )
    return rc;
  return session.run();
}
