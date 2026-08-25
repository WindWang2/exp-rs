#pragma once
#if defined(_WIN32) || defined(_WIN64)
#if defined(sicnu_agent_EXPORTS)
#define SICNU_AGENT_EXPORT __declspec(dllexport)
#else
#define SICNU_AGENT_EXPORT __declspec(dllimport)
#endif
#else
#define SICNU_AGENT_EXPORT
#endif
