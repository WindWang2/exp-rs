#pragma once
// ANTIGRAVITY: QScintilla stub
#define QSCINTILLA_VERSION 0x020d00
#define QSCINTILLA_VERSION_STR "2.13.0"
#if defined(_WIN32) || defined(_WIN64)
#if defined(qgis_gui_EXPORTS)
#define QSCINTILLA_EXPORT __declspec(dllexport)
#else
#define QSCINTILLA_EXPORT __declspec(dllimport)
#endif
#else
#define QSCINTILLA_EXPORT
#endif
