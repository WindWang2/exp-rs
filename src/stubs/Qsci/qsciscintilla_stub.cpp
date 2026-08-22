// Stub implementation for QScintilla classes.
// Defines the key virtual functions (destructors + MOC stubs) out-of-line so
// the compiler emits typeinfo and vtable symbols into libqgis_gui.so.
// Without these, ASAN-enabled builds fail at runtime with:
//   symbol lookup error: undefined symbol: _ZTV15QsciLexerPython etc.
//
// These stubs have Q_OBJECT macros but their headers live in src/stubs/Qsci/,
// outside the gui source tree. AUTOMOC cannot process headers from outside the
// target's source directory, so we manually provide the MOC key functions
// (metaObject, qt_metacast, qt_metacall) to force vtable/typeinfo emission.
#include "qsciscintillabase.h"
#include "qsciscintilla.h"
#include "qscilexer.h"
#include "qscilexercss.h"
#include "qscilexersql.h"
#include "qscilexerpython.h"
#include "qsciapis.h"

// --- Destructors (force vtable/typeinfo via "key function" rule) ---
QsciScintillaBase::~QsciScintillaBase() {}
QsciScintilla::~QsciScintilla() {}
QsciLexer::~QsciLexer() {}
QsciLexerCSS::~QsciLexerCSS() {}
QsciLexerSQL::~QsciLexerSQL() {}
QsciLexerPython::~QsciLexerPython() {}
QsciAPIs::~QsciAPIs() {}

