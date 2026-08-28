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

// --- MOC Stubs for classes outside AUTOMOC scan scope ---
// HAZARD (#634): these fake metaObject()s alias the BASE class meta-object,
// so qobject_cast<QsciLexerHTML*>(any QsciLexer*) succeeds and distinct
// lexer subclasses are indistinguishable. Safe only while the stub lexers
// have no virtual behavior and nobody qobject_casts to a subclass; if these
// classes are ever really enabled, give each its own Q_OBJECT meta-object.
const QMetaObject *QsciLexerHTML::metaObject() const { return &QsciLexer::staticMetaObject; }
void *QsciLexerHTML::qt_metacast( const char *c ) { return QsciLexer::qt_metacast( c ); }
int QsciLexerHTML::qt_metacall( QMetaObject::Call c, int id, void **a ) { return QsciLexer::qt_metacall( c, id, a ); }

const QMetaObject *QsciLexerJavaScript::metaObject() const { return &QsciLexer::staticMetaObject; }
void *QsciLexerJavaScript::qt_metacast( const char *c ) { return QsciLexer::qt_metacast( c ); }
int QsciLexerJavaScript::qt_metacall( QMetaObject::Call c, int id, void **a ) { return QsciLexer::qt_metacall( c, id, a ); }

const QMetaObject *QsciLexerJSON::metaObject() const { return &QsciLexer::staticMetaObject; }
void *QsciLexerJSON::qt_metacast( const char *c ) { return QsciLexer::qt_metacast( c ); }
int QsciLexerJSON::qt_metacall( QMetaObject::Call c, int id, void **a ) { return QsciLexer::qt_metacall( c, id, a ); }

