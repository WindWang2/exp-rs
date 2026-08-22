#pragma once

#if defined(_WIN32) || defined(_WIN64)
#if defined(sicnu_processing_EXPORTS)
#define SICNU_PROCESSING_EXPORT __declspec(dllexport)
#else
#define SICNU_PROCESSING_EXPORT __declspec(dllimport)
#endif
#else
#define SICNU_PROCESSING_EXPORT
#endif
