// ANTIGRAVITY: Minimal stub implementations for removed codeeditor classes.
// These exist only to satisfy qgsgui.cpp's use of QgsCodeEditorColorSchemeRegistry
// and to provide MOC key-function stubs + vtable/typeinfo for QgsCodeEditor subclasses.
// Without these stubs, ASAN builds fail with:
//   symbol lookup error: undefined symbol: _ZTI13QgsCodeEditor
//   symbol lookup error: undefined symbol: _ZN13QgsCodeEditor5eventEP6QEvent
#include "codeeditors/qgscodeeditorcolorscheme.h"
#include "codeeditors/qgscodeeditorcolorschemeregistry.h"
#include "codeeditors/qgscodeeditor.h"
#include "codeeditors/qgscodeeditorsql.h"
#include "codeeditors/qgscodeeditorexpression.h"

QgsCodeEditorColorScheme::QgsCodeEditorColorScheme(const QString &id, const QString &name)
    : mId(id), mThemeName(name) {}

QColor QgsCodeEditorColorScheme::color(ColorRole) const { return QColor(); }
void QgsCodeEditorColorScheme::setColor(ColorRole, const QColor &) {}
void QgsCodeEditorColorScheme::setColors(const QMap<ColorRole, QColor> &) {}

QgsCodeEditorColorSchemeRegistry::QgsCodeEditorColorSchemeRegistry() = default;
bool QgsCodeEditorColorSchemeRegistry::addColorScheme(const QgsCodeEditorColorScheme &) { return false; }
bool QgsCodeEditorColorSchemeRegistry::removeColorScheme(const QString &) { return false; }
QStringList QgsCodeEditorColorSchemeRegistry::schemes() const { return {}; }
QgsCodeEditorColorScheme QgsCodeEditorColorSchemeRegistry::scheme(const QString &) const { return {}; }

// MOC key-function stubs: QgsCodeEditor has Q_OBJECT but its headers are removed
// from the target to prevent AUTOMOC. Without MOC, the virtual functions declared
// by Q_OBJECT (metaObject, qt_metacast, qt_metacall) are never defined, so the
// compiler never emits vtable + typeinfo. Defining these here forces emission.
const QMetaObject *QgsCodeEditor::metaObject() const
{
  return &QsciScintilla::staticMetaObject;
}
void *QgsCodeEditor::qt_metacast( const char * )
{
  return nullptr;
}
int QgsCodeEditor::qt_metacall( QMetaObject::Call, int, void ** )
{
  return -1;
}
bool QgsCodeEditor::event( QEvent * )
{
  return false;
}
Qgis::ScriptLanguage QgsCodeEditor::language() const
{
  return Qgis::ScriptLanguage::Unknown;
}
Qgis::ScriptLanguageCapabilities QgsCodeEditor::languageCapabilities() const
{
  return Qgis::ScriptLanguageCapabilities();
}
void QgsCodeEditor::initializeLexer() {}
void QgsCodeEditor::populateContextMenu( QMenu * ) {}
QString QgsCodeEditor::reformatCodeString( const QString &s ) { return s; }
void QgsCodeEditor::showMessage( const QString &, const QString &, Qgis::MessageLevel ) {}
void QgsCodeEditor::moveCursorToStart() {}
void QgsCodeEditor::moveCursorToEnd() {}
bool QgsCodeEditor::checkSyntax() { return false; }
void QgsCodeEditor::toggleComment() {}
bool QgsCodeEditor::eventFilter( QObject *, QEvent * ) { return false; }
void QgsCodeEditor::focusOutEvent( QFocusEvent * ) {}
void QgsCodeEditor::keyPressEvent( QKeyEvent * ) {}
void QgsCodeEditor::contextMenuEvent( QContextMenuEvent * ) {}
void QgsCodeEditor::callTip() {}
void QgsCodeEditor::setText( const QString & ) {}

// QgsCodeEditorSQL MOC key-function stubs
const QMetaObject *QgsCodeEditorSQL::metaObject() const
{
  return &QsciScintilla::staticMetaObject;
}
void *QgsCodeEditorSQL::qt_metacast( const char * )
{
  return nullptr;
}
int QgsCodeEditorSQL::qt_metacall( QMetaObject::Call, int, void ** )
{
  return -1;
}
Qgis::ScriptLanguage QgsCodeEditorSQL::language() const
{
  return Qgis::ScriptLanguage::Sql;
}
void QgsCodeEditorSQL::initializeLexer() {}
QgsCodeEditorSQL::~QgsCodeEditorSQL() {}

// QgsCodeEditorExpression MOC key-function stubs
const QMetaObject *QgsCodeEditorExpression::metaObject() const
{
  return &QsciScintilla::staticMetaObject;
}
void *QgsCodeEditorExpression::qt_metacast( const char * )
{
  return nullptr;
}
int QgsCodeEditorExpression::qt_metacall( QMetaObject::Call, int, void ** )
{
  return -1;
}
Qgis::ScriptLanguage QgsCodeEditorExpression::language() const
{
  return Qgis::ScriptLanguage::QgisExpression;
}
void QgsCodeEditorExpression::initializeLexer() {}
void QgsCodeEditorExpression::toggleComment() {}
Qgis::ScriptLanguageCapabilities QgsCodeEditorExpression::languageCapabilities() const
{
  return Qgis::ScriptLanguageCapabilities();
}
