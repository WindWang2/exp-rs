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

// --- staticMetaObject definition ---
// QsciLexer has Q_OBJECT but its MOC is not compiled (header outside target source tree).
// We must define the staticMetaObject data member that Q_OBJECT declares.
// QMetaObject is an aggregate; initialize d with superdata pointing to QObject.
const QMetaObject QsciLexer::staticMetaObject = { {
    { &QObject::staticMetaObject },
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
} };

// --- MOC key-function stubs ---
// The "key function" for Q_OBJECT classes is metaObject(), which MOC would
// normally generate. Since AUTOMOC can't see these headers (they're outside
// the gui source tree), we provide manual definitions to anchor vtable/typeinfo.
// NOTE: QsciScintillaBase and QsciScintilla already have AUTOMOC-generated
// definitions (their headers ARE listed in the target sources), so we only
// provide stubs for the classes whose MOC files are NOT compiled.

const QMetaObject *QsciLexer::metaObject() const { return &QObject::staticMetaObject; }
void *QsciLexer::qt_metacast(const char *) { return nullptr; }
int QsciLexer::qt_metacall(QMetaObject::Call, int, void **) { return -1; }

const QMetaObject *QsciLexerCSS::metaObject() const { return &QsciLexer::staticMetaObject; }
void *QsciLexerCSS::qt_metacast(const char *) { return nullptr; }
int QsciLexerCSS::qt_metacall(QMetaObject::Call, int, void **) { return -1; }

const QMetaObject *QsciLexerSQL::metaObject() const { return &QsciLexer::staticMetaObject; }
void *QsciLexerSQL::qt_metacast(const char *) { return nullptr; }
int QsciLexerSQL::qt_metacall(QMetaObject::Call, int, void **) { return -1; }

const QMetaObject *QsciLexerPython::metaObject() const { return &QsciLexer::staticMetaObject; }
void *QsciLexerPython::qt_metacast(const char *) { return nullptr; }
int QsciLexerPython::qt_metacall(QMetaObject::Call, int, void **) { return -1; }

const QMetaObject *QsciAPIs::metaObject() const { return &QObject::staticMetaObject; }
void *QsciAPIs::qt_metacast(const char *) { return nullptr; }
int QsciAPIs::qt_metacall(QMetaObject::Call, int, void **) { return -1; }
