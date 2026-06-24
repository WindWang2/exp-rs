#pragma once

// Minimal compatibility header for ported QGIS app-level tools
// On Linux/Mac APP_EXPORT is empty; on Windows it would be __declspec(dllexport)

#ifndef APP_EXPORT
#  ifdef _MSC_VER
#    define APP_EXPORT __declspec(dllexport)
#  else
#    define APP_EXPORT
#  endif
#endif

class QgisApp;
