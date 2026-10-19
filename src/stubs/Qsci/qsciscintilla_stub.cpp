// Stub implementation for QScintilla classes.
// Defines the key virtual functions (destructors) out-of-line so
// the compiler emits typeinfo and vtable symbols into libqgis_gui.so.
// Without these, ASAN-enabled builds fail at runtime with:
//   symbol lookup error: undefined symbol: _ZTV15QsciLexerPython etc.
//
// These stubs have Q_OBJECT macros; their headers under src/stubs/Qsci/ are
// listed in src/gui/CMakeLists.txt so AUTOMOC generates their meta-objects.
// This file itself only supplies the out-of-line destructors (see below).
#include "qsciscintillabase.h"
#include "qsciscintilla.h"
#include "qscilexer.h"
#include "qscilexercss.h"
#include "qscilexersql.h"
#include "qscilexerpython.h"
#include "qscilexerhtml.h"
#include "qscilexerjavascript.h"
#include "qscilexerjson.h"
#include "qsciapis.h"

// --- Destructors (force vtable/typeinfo via "key function" rule) ---
QsciScintillaBase::~QsciScintillaBase() {}
QsciScintilla::~QsciScintilla() {}
QsciLexer::~QsciLexer() {}
QsciLexerCSS::~QsciLexerCSS() {}
QsciLexerSQL::~QsciLexerSQL() {}
QsciLexerPython::~QsciLexerPython() {}
QsciLexerHTML::~QsciLexerHTML() {}
QsciLexerJavaScript::~QsciLexerJavaScript() {}
QsciLexerJSON::~QsciLexerJSON() {}
QsciAPIs::~QsciAPIs() {}

// --- MOC: AUTOMOC owns the meta-objects; no hand-written aliases here ---
// Every stub header (qsciscintillabase, qsciscintilla, qscilexer, the
// CSS / SQL / Python / HTML / JavaScript / JSON lexers, and qsciapis) is
// listed in src/gui/CMakeLists.txt, so AUTOMOC emits a real per-class
// meta-object for each - not just the HTML / JavaScript / JSON trio the old
// note named (CSS / SQL / Python joined the list in de1ea0d4ae). No
// hand-written metaObject/qt_metacast aliases are kept here - they would
// shadow the generated ones and collide (LNK2005). The #634 warning about
// fake metaObject()s aliasing the BASE class described the pre-listing state
// and no longer applies.
