#ifndef QGIS_ANALYSIS_EXPORT_H
#define QGIS_ANALYSIS_EXPORT_H

#if defined(_WIN32) || defined(_WIN64)
  #if defined(qgis_analysis_SHARED) || defined(QGIS_ANALYSIS_SHARED)
    #ifdef qgis_analysis_EXPORTS
      #define QGIS_ANALYSIS_EXPORT __declspec(dllexport)
    #else
      #define QGIS_ANALYSIS_EXPORT __declspec(dllimport)
    #endif
  #else
    #define QGIS_ANALYSIS_EXPORT
  #endif
#else
  #define QGIS_ANALYSIS_EXPORT __attribute__((visibility("default")))
#endif

#endif
