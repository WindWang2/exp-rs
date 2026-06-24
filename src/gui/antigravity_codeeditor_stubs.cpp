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
#include "codeeditors/qgscodeeditorhtml.h"

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

// Constructors: must properly initialize QObject chain so that setupUi() can call
// setObjectName() on code editor widgets without crashing. The base class chain is:
// QgsCodeEditorSQL -> QgsCodeEditor -> QsciScintilla -> QsciScintillaBase -> QAbstractScrollArea
QgsCodeEditor::QgsCodeEditor( QWidget *parent, const QString &title, bool folding, bool margin, QgsCodeEditor::Flags flags, QgsCodeEditor::Mode mode )
  : QsciScintilla( parent )
  , mWidgetTitle( title )
  , mMargin( margin )
  , mFlags( flags )
  , mMode( mode )
{
  Q_UNUSED( folding )
}

QgsCodeEditorSQL::QgsCodeEditorSQL( QWidget *parent )
  : QgsCodeEditor( parent )
{
}

QgsCodeEditorExpression::QgsCodeEditorExpression( QWidget *parent )
  : QgsCodeEditor( parent )
{
}

QgsCodeEditorHTML::QgsCodeEditorHTML( QWidget *parent )
  : QgsCodeEditor( parent )
{
}

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

// QgsCodeEditorHTML MOC key-function stubs
const QMetaObject *QgsCodeEditorHTML::metaObject() const
{
  return &QsciScintilla::staticMetaObject;
}
void *QgsCodeEditorHTML::qt_metacast( const char * )
{
  return nullptr;
}
int QgsCodeEditorHTML::qt_metacall( QMetaObject::Call, int, void ** )
{
  return -1;
}
Qgis::ScriptLanguage QgsCodeEditorHTML::language() const
{
  return Qgis::ScriptLanguage::Html;
}
void QgsCodeEditorHTML::initializeLexer() {}
Qgis::ScriptLanguageCapabilities QgsCodeEditorHTML::languageCapabilities() const
{
  return Qgis::ScriptLanguageCapabilities();
}
QString QgsCodeEditorHTML::reformatCodeString( const QString &s ) { return s; }
void QgsCodeEditorHTML::toggleComment() {}

// ============================================================================
// ANTIGRAVITY: QgsAttributesFormProperties stubs
// This .cpp is REMOVED from the build because it includes qgscodeeditor.h.
// The real constructor calls setupUi(this) which creates QgsCodeEditor widgets.
// We provide a minimal constructor that initializes QWidget and creates an empty
// layout so that layout()->setContentsMargins() doesn't crash.
// ============================================================================
#include "vector/qgsattributesformproperties.h"
#include "vector/qgsattributesformview.h"
#include <QVBoxLayout>

QgsAttributesFormProperties::QgsAttributesFormProperties( QgsVectorLayer *layer, QWidget *parent, QgsSourceFieldsProperties *sourceFieldsProperties )
  : QWidget( parent )
  , mLayer( layer )
  , mSourceFieldsProperties( sourceFieldsProperties )
{
  setLayout( new QVBoxLayout( this ) );
}

void QgsAttributesFormProperties::init() {}
void QgsAttributesFormProperties::apply() {}
void QgsAttributesFormProperties::store() {}
void QgsAttributesFormProperties::loadRelations() {}
void QgsAttributesFormProperties::initAvailableWidgetsView() {}
void QgsAttributesFormProperties::initFormLayoutView() {}
void QgsAttributesFormProperties::initLayoutConfig() {}
void QgsAttributesFormProperties::initInitPython() {}
void QgsAttributesFormProperties::initSuppressCombo() {}
void QgsAttributesFormProperties::initAvailableWidgetsActions( const QList<QgsAction> ) {}
QgsExpressionContext QgsAttributesFormProperties::createExpressionContext() const { return QgsExpressionContext(); }
void QgsAttributesFormProperties::updateButtons() {}

// NOTE: No MOC stubs needed — headers remain in QGIS_GUI_HDRS so AUTOMOC
// generates metaObject/qt_metacast/qt_metacall.  Only the .cpp bodies are
// removed from the build, hence the stubs above.

// QgsAttributesFormBaseView stubs
QgsAttributesFormBaseView::QgsAttributesFormBaseView( QgsVectorLayer *layer, QWidget *parent )
  : QTreeView( parent )
  , mLayer( layer )
{
}

QModelIndex QgsAttributesFormBaseView::firstSelectedIndex() const { return QModelIndex(); }
QgsExpressionContext QgsAttributesFormBaseView::createExpressionContext() const { return QgsExpressionContext(); }
const QList<QgsAttributesFormTreeViewIndicator *> QgsAttributesFormBaseView::indicators( const QModelIndex & ) const { return {}; }
const QList<QgsAttributesFormTreeViewIndicator *> QgsAttributesFormBaseView::indicators( QgsAttributesFormItem * ) const { return {}; }
void QgsAttributesFormBaseView::addIndicator( QgsAttributesFormItem *, QgsAttributesFormTreeViewIndicator * ) {}
void QgsAttributesFormBaseView::removeIndicator( QgsAttributesFormItem *, QgsAttributesFormTreeViewIndicator * ) {}
void QgsAttributesFormBaseView::removeAllIndicators() {}
QgsAttributesFormModel *QgsAttributesFormBaseView::sourceModel() const { return nullptr; }
void QgsAttributesFormBaseView::selectFirstMatchingItem( const QgsAttributesFormData::AttributesFormItemType &, const QString & ) {}
void QgsAttributesFormBaseView::setFilterText( const QString & ) {}

// QgsAttributesAvailableWidgetsView stubs
QgsAttributesAvailableWidgetsView::QgsAttributesAvailableWidgetsView( QgsVectorLayer *layer, QWidget *parent )
  : QgsAttributesFormBaseView( layer, parent )
{
}

void QgsAttributesAvailableWidgetsView::setModel( QAbstractItemModel * ) {}
QgsAttributesAvailableWidgetsModel *QgsAttributesAvailableWidgetsView::availableWidgetsModel() const { return nullptr; }

// QgsAttributesFormLayoutView stubs
QgsAttributesFormLayoutView::QgsAttributesFormLayoutView( QgsVectorLayer *layer, QWidget *parent )
  : QgsAttributesFormBaseView( layer, parent )
{
}

void QgsAttributesFormLayoutView::setModel( QAbstractItemModel * ) {}
void QgsAttributesFormLayoutView::dragEnterEvent( QDragEnterEvent * ) {}
void QgsAttributesFormLayoutView::dragMoveEvent( QDragMoveEvent * ) {}
void QgsAttributesFormLayoutView::dropEvent( QDropEvent * ) {}
