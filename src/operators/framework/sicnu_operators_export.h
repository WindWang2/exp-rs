#pragma once

#if defined(_WIN32) || defined(_WIN64)
#if defined(sicnu_operators_EXPORTS)
#define SICNU_OPERATORS_EXPORT __declspec(dllexport)
#else
#define SICNU_OPERATORS_EXPORT __declspec(dllimport)
#endif
#else
#define SICNU_OPERATORS_EXPORT
#endif
