#pragma once

#if defined(_WIN32) || defined(_WIN64)
#if defined(sicnu_workflow_EXPORTS)
#define SICNU_WORKFLOW_EXPORT __declspec(dllexport)
#else
#define SICNU_WORKFLOW_EXPORT __declspec(dllimport)
#endif
#else
#define SICNU_WORKFLOW_EXPORT
#endif
