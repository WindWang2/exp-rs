#include "antigravity_init.h"
#include <qgsapplication.h>
#include <mutex>
static std::once_flag g_once;
void antigravity_init(const QString &dataRoot) {
    std::call_once(g_once, [&]() {
        static int argc_val = 1;
        static char appName[] = "antigravity";
        static char *argv_buf[] = {appName, nullptr};
        // Heap-alloc: no destructor → avoids QgsCoordinateTransformPrivate::freeProj
        // crash during DSO __do_global_dtors_aux (test binaries use _exit to skip teardown).
        new QgsApplication(argc_val, argv_buf, /*GUIenabled=*/true);
        QgsApplication::setPrefixPath(dataRoot, true);
        QgsApplication::initQgis();
    });
}
