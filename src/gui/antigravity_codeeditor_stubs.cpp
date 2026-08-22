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
#include "codeeditors/qgscodeeditorjson.h"
#include "codeeditors/qgscodeeditorpython.h"

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

QFont QgsCodeEditor::getMonospaceFont()
{
  QFont font( QStringLiteral( "monospace" ) );
  font.setStyleHint( QFont::Monospace );
  return font;
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
// from the target to prevent AUTOMOC. Forward to QsciScintilla which has full real MOC.
const QMetaObject QgsCodeEditor::staticMetaObject = QsciScintilla::staticMetaObject;
const QMetaObject QgsCodeEditorExpression::staticMetaObject = QsciScintilla::staticMetaObject;
const QMetaObject QgsCodeEditorHTML::staticMetaObject = QsciScintilla::staticMetaObject;
const QMetaObject QgsCodeEditorJson::staticMetaObject = QsciScintilla::staticMetaObject;
const QMetaObject QgsCodeEditorPython::staticMetaObject = QsciScintilla::staticMetaObject;
const QMetaObject QgsCodeEditorSQL::staticMetaObject = QsciScintilla::staticMetaObject;

const QgsSettingsEntryString *QgsCodeEditorPython::settingCodeFormatter = nullptr;
const QgsSettingsEntryInteger *QgsCodeEditorPython::settingMaxLineLength = nullptr;
const QgsSettingsEntryBool *QgsCodeEditorPython::settingSortImports = nullptr;
const QgsSettingsEntryInteger *QgsCodeEditorPython::settingAutopep8Level = nullptr;
const QgsSettingsEntryBool *QgsCodeEditorPython::settingBlackNormalizeQuotes = nullptr;
const QgsSettingsEntryString *QgsCodeEditorPython::settingExternalPythonEditorCommand = nullptr;
const QgsSettingsEntryEnumFlag<Qgis::DocumentationBrowser> *QgsCodeEditorPython::settingContextHelpBrowser = nullptr;

QgsCodeInterpreter::~QgsCodeInterpreter() = default;

// MOC key-function stubs:
const QMetaObject *QgsCodeEditor::metaObject() const
{
  return &QsciScintilla::staticMetaObject;
}
void *QgsCodeEditor::qt_metacast( const char *c )
{
  return QsciScintilla::qt_metacast( c );
}
int QgsCodeEditor::qt_metacall( QMetaObject::Call c, int id, void **a )
{
  return QsciScintilla::qt_metacall( c, id, a );
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
void QgsCodeEditor::setText( const QString &text ) { QsciScintilla::setText( text ); }
void QgsCodeEditor::insertText( const QString & ) {}
void QgsCodeEditor::setLineNumbersVisible( bool ) {}
void QgsCodeEditor::setLinearSelection( int, int ) {}
int QgsCodeEditor::selectionStart() const { return 0; }
int QgsCodeEditor::selectionEnd() const { return 0; }

// QgsCodeEditorSQL MOC key-function stubs
const QMetaObject *QgsCodeEditorSQL::metaObject() const
{
  return &QsciScintilla::staticMetaObject;
}
void *QgsCodeEditorSQL::qt_metacast( const char *c )
{
  return QsciScintilla::qt_metacast( c );
}
int QgsCodeEditorSQL::qt_metacall( QMetaObject::Call c, int id, void **a )
{
  return QsciScintilla::qt_metacall( c, id, a );
}
Qgis::ScriptLanguage QgsCodeEditorSQL::language() const
{
  return Qgis::ScriptLanguage::Sql;
}
void QgsCodeEditorSQL::initializeLexer() {}
QgsCodeEditorSQL::~QgsCodeEditorSQL() {}
void QgsCodeEditorSQL::setFields( const QgsFields & ) {}

// QgsCodeEditorExpression MOC key-function stubs
const QMetaObject *QgsCodeEditorExpression::metaObject() const
{
  return &QsciScintilla::staticMetaObject;
}
void *QgsCodeEditorExpression::qt_metacast( const char *c )
{
  return QsciScintilla::qt_metacast( c );
}
int QgsCodeEditorExpression::qt_metacall( QMetaObject::Call c, int id, void **a )
{
  return QsciScintilla::qt_metacall( c, id, a );
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
void *QgsCodeEditorHTML::qt_metacast( const char *c )
{
  return QsciScintilla::qt_metacast( c );
}
int QgsCodeEditorHTML::qt_metacall( QMetaObject::Call c, int id, void **a )
{
  return QsciScintilla::qt_metacall( c, id, a );
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

// QgsCodeEditorJson stubs
const QMetaObject *QgsCodeEditorJson::metaObject() const { return &QsciScintilla::staticMetaObject; }
void *QgsCodeEditorJson::qt_metacast( const char *c ) { return QsciScintilla::qt_metacast( c ); }
int QgsCodeEditorJson::qt_metacall( QMetaObject::Call c, int id, void **a ) { return QsciScintilla::qt_metacall( c, id, a ); }
QgsCodeEditorJson::QgsCodeEditorJson( QWidget *parent ) : QgsCodeEditor( parent ) {}
Qgis::ScriptLanguage QgsCodeEditorJson::language() const { return Qgis::ScriptLanguage::Json; }
void QgsCodeEditorJson::initializeLexer() {}

// QgsCodeEditorPython stubs
const QMetaObject *QgsCodeEditorPython::metaObject() const { return &QsciScintilla::staticMetaObject; }
void *QgsCodeEditorPython::qt_metacast( const char *c ) { return QsciScintilla::qt_metacast( c ); }
int QgsCodeEditorPython::qt_metacall( QMetaObject::Call c, int id, void **a ) { return QsciScintilla::qt_metacall( c, id, a ); }
QgsCodeEditorPython::QgsCodeEditorPython( QWidget *parent, const QList<QString> &, QgsCodeEditor::Mode mode, QgsCodeEditor::Flags flags )
  : QgsCodeEditor( parent, QString(), false, false, flags, mode ) {}
Qgis::ScriptLanguage QgsCodeEditorPython::language() const { return Qgis::ScriptLanguage::Python; }
Qgis::ScriptLanguageCapabilities QgsCodeEditorPython::languageCapabilities() const { return Qgis::ScriptLanguageCapabilities(); }
void QgsCodeEditorPython::loadAPIs( const QList<QString> & ) {}
bool QgsCodeEditorPython::loadScript( const QString & ) { return false; }
bool QgsCodeEditorPython::checkSyntax() { return false; }
void QgsCodeEditorPython::showApiDocumentation( const QString & ) {}
void QgsCodeEditorPython::toggleComment() {}
void QgsCodeEditorPython::initializeLexer() {}
void QgsCodeEditorPython::keyPressEvent( QKeyEvent * ) {}
QString QgsCodeEditorPython::reformatCodeString( const QString &s ) { return s; }
void QgsCodeEditorPython::populateContextMenu( QMenu * ) {}

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
void QgsAttributesFormProperties::addContainer() {}
void QgsAttributesFormProperties::loadAttributeSpecificEditor( QgsAttributesFormBaseView *, QgsAttributesFormBaseView *, QModelIndex & ) {}
void QgsAttributesFormProperties::mEditorLayoutComboBox_currentIndexChanged( int ) {}
void QgsAttributesFormProperties::mTbInitCode_clicked() {}
void QgsAttributesFormProperties::onAttributeSelectionChanged( const QItemSelection &, const QItemSelection & ) {}
void QgsAttributesFormProperties::onContextMenuRequested( QPoint ) {}
void QgsAttributesFormProperties::onFormLayoutSelectionChanged( const QItemSelection &, const QItemSelection & ) {}
void QgsAttributesFormProperties::onInvertSelectionButtonClicked( bool ) {}
void QgsAttributesFormProperties::pbnSelectEditForm_clicked() {}
void QgsAttributesFormProperties::previewForm() {}
void QgsAttributesFormProperties::removeTabOrGroupButton() {}
void QgsAttributesFormProperties::toggleShowAliases( bool ) {}
void QgsAttributesFormProperties::updateFilteredItems( const QString & ) {}
void QgsAttributesFormProperties::updatedFields() {}

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
void QgsAttributesFormLayoutView::onItemDoubleClicked( const QModelIndex & ) {}
void QgsAttributesFormLayoutView::handleExternalDroppedItem( QModelIndex & ) {}
void QgsAttributesFormLayoutView::handleInternalDroppedItem( QModelIndex & ) {}

// ============================================================================
// Safe C++ stubs for reachable trimmed widgets (Issue #290)
// ============================================================================

// --- QgsExpressionLineEdit ---
#include "qgsexpressionlineedit.h"
#include <QHBoxLayout>

QgsExpressionLineEdit::QgsExpressionLineEdit( QWidget *parent )
  : QWidget( parent )
{
  auto *l = new QHBoxLayout( this );
  l->setContentsMargins( 0, 0, 0, 0 );
}

QgsExpressionLineEdit::~QgsExpressionLineEdit() = default;

void QgsExpressionLineEdit::setExpressionDialogTitle( const QString &title ) { mExpressionDialogTitle = title; }
void QgsExpressionLineEdit::setMultiLine( bool ) {}
QString QgsExpressionLineEdit::expectedOutputFormat() const { return mExpectedOutputFormat; }
void QgsExpressionLineEdit::setExpectedOutputFormat( const QString &expected ) { mExpectedOutputFormat = expected; }
void QgsExpressionLineEdit::setGeomCalculator( const QgsDistanceArea & ) {}
void QgsExpressionLineEdit::setLayer( QgsVectorLayer *layer ) { mLayer = layer; }
QString QgsExpressionLineEdit::expression() const { return property( "_qgs_expression" ).toString(); }
bool QgsExpressionLineEdit::isValidExpression( QString * ) const { return true; }
void QgsExpressionLineEdit::registerExpressionContextGenerator( const QgsExpressionContextGenerator *g ) { mExpressionContextGenerator = g; }
void QgsExpressionLineEdit::setExpression( const QString &expression )
{
  setProperty( "_qgs_expression", expression );
  emit expressionChanged( expression );
}
void QgsExpressionLineEdit::changeEvent( QEvent *event ) { QWidget::changeEvent( event ); }
void QgsExpressionLineEdit::expressionEdited( const QString &expression ) { setExpression( expression ); }
void QgsExpressionLineEdit::expressionEdited() {}
void QgsExpressionLineEdit::editExpression() {}
void QgsExpressionLineEdit::updateLineEditStyle( const QString & ) {}
bool QgsExpressionLineEdit::isExpressionValid( const QString & ) { return true; }

// --- QgsExpressionBuilderWidget ---
#include "qgsexpressionbuilderwidget.h"

QgsExpressionBuilderWidget::QgsExpressionBuilderWidget( QWidget *parent )
  : QWidget( parent )
{
  auto *l = new QVBoxLayout( this );
  l->setContentsMargins( 0, 0, 0, 0 );
}

QgsExpressionBuilderWidget::~QgsExpressionBuilderWidget() = default;

void QgsExpressionBuilderWidget::init( const QgsExpressionContext &, const QString &, QgsExpressionBuilderWidget::Flags ) {}
void QgsExpressionBuilderWidget::initWithLayer( QgsVectorLayer *layer, const QgsExpressionContext &, const QString &, QgsExpressionBuilderWidget::Flags ) { setLayer( layer ); }
void QgsExpressionBuilderWidget::initWithFields( const QgsFields &, const QgsExpressionContext &, const QString &, QgsExpressionBuilderWidget::Flags ) {}
void QgsExpressionBuilderWidget::setLayer( QgsVectorLayer *layer ) { mLayer = layer; }
QgsVectorLayer *QgsExpressionBuilderWidget::layer() const { return mLayer; }
void QgsExpressionBuilderWidget::loadFieldsAndValues( const QMap<QString, QStringList> & ) {}
void QgsExpressionBuilderWidget::setGeomCalculator( const QgsDistanceArea & ) {}
QString QgsExpressionBuilderWidget::expressionText() { return property( "_qgs_expression" ).toString(); }
void QgsExpressionBuilderWidget::setExpressionText( const QString &expression )
{
  setProperty( "_qgs_expression", expression );
  emit expressionParsed( true );
}
QString QgsExpressionBuilderWidget::expectedOutputFormat() { return property( "_qgs_expected_format" ).toString(); }
void QgsExpressionBuilderWidget::setExpectedOutputFormat( const QString &expected ) { setProperty( "_qgs_expected_format", expected ); }
void QgsExpressionBuilderWidget::setExpressionContext( const QgsExpressionContext &context ) { mExpressionContext = context; }
bool QgsExpressionBuilderWidget::isExpressionValid() { return true; }
void QgsExpressionBuilderWidget::setCustomPreviewGenerator( const QString &, const QList<QPair<QString, QVariant>> &, const std::function<QgsExpressionContext( const QVariant & )> & ) {}
QgsExpressionTreeView *QgsExpressionBuilderWidget::expressionTree() const { return nullptr; }

// --- QgsHistogramWidget ---
#include "qgshistogramwidget.h"

const QgsSettingsEntryBool *QgsHistogramWidget::settingsHistogramShowMean = nullptr;
const QgsSettingsEntryBool *QgsHistogramWidget::settingsHistogramShowStdev = nullptr;

QgsHistogramWidget::QgsHistogramWidget( QWidget *parent, QgsVectorLayer *layer, const QString &fieldOrExp )
  : QWidget( parent )
  , mVectorLayer( layer )
  , mSourceFieldExp( fieldOrExp )
{
  setLayout( new QVBoxLayout( this ) );
}

QgsHistogramWidget::~QgsHistogramWidget() = default;

void QgsHistogramWidget::setLayer( QgsVectorLayer *layer ) { mVectorLayer = layer; }
void QgsHistogramWidget::setSourceFieldExp( const QString &fieldOrExp ) { mSourceFieldExp = fieldOrExp; }
void QgsHistogramWidget::setGraduatedRanges( const QgsRangeList &ranges ) { mRanges = ranges; }
void QgsHistogramWidget::refreshValues() {}
void QgsHistogramWidget::refresh() {}
void QgsHistogramWidget::drawHistogram() {}
void QgsHistogramWidget::clearHistogram() {}
QwtPlotHistogram *QgsHistogramWidget::createPlotHistogram( const QString &, const QBrush &, const QPen & ) const { return nullptr; }

// --- QgsGraduatedHistogramWidget ---
#include "symbology/qgsgraduatedhistogramwidget.h"

QgsGraduatedHistogramWidget::QgsGraduatedHistogramWidget( QWidget *parent )
  : QgsHistogramWidget( parent )
{
}

void QgsGraduatedHistogramWidget::setRenderer( QgsGraduatedSymbolRenderer *renderer ) { mRenderer = renderer; }
void QgsGraduatedHistogramWidget::drawHistogram() {}
void QgsGraduatedHistogramWidget::mousePress( double ) {}
void QgsGraduatedHistogramWidget::mouseRelease( double ) {}
void QgsGraduatedHistogramWidget::findClosestRange( double, int &, int & ) const {}
QwtPlotHistogram *QgsGraduatedHistogramWidget::createPlotHistogram( const QString &, const QColor & ) const { return nullptr; }

QgsGraduatedHistogramEventFilter::QgsGraduatedHistogramEventFilter( QwtPlot *plot )
  : QObject( plot )
{
}

bool QgsGraduatedHistogramEventFilter::eventFilter( QObject *, QEvent * ) { return false; }

// --- QgsCurveEditorWidget ---
#include "qgscurveeditorwidget.h"

QgsCurveEditorWidget::QgsCurveEditorWidget( QWidget *parent, const QgsCurveTransform &curve )
  : QWidget( parent )
  , mCurve( curve )
{
  setLayout( new QVBoxLayout( this ) );
}

QgsCurveEditorWidget::~QgsCurveEditorWidget() = default;

void QgsCurveEditorWidget::setCurve( const QgsCurveTransform &curve ) { mCurve = curve; emit changed(); }
void QgsCurveEditorWidget::setHistogramSource( const QgsVectorLayer *, const QString & ) {}
void QgsCurveEditorWidget::setMinHistogramValueRange( double min ) { mMinValueRange = min; }
void QgsCurveEditorWidget::setMaxHistogramValueRange( double max ) { mMaxValueRange = max; }
void QgsCurveEditorWidget::keyPressEvent( QKeyEvent *e ) { QWidget::keyPressEvent( e ); }
void QgsCurveEditorWidget::plotMousePress( QPointF ) {}
void QgsCurveEditorWidget::plotMouseRelease( QPointF ) {}
void QgsCurveEditorWidget::plotMouseMove( QPointF ) {}
void QgsCurveEditorWidget::updatePlot() {}
void QgsCurveEditorWidget::addPlotMarker( double, double, bool ) {}
void QgsCurveEditorWidget::updateHistogram() {}
int QgsCurveEditorWidget::findNearestControlPoint( QPointF ) const { return -1; }
QwtPlotHistogram *QgsCurveEditorWidget::createPlotHistogram( const QBrush &, const QPen & ) const { return nullptr; }

QgsCurveEditorPlotEventFilter::QgsCurveEditorPlotEventFilter( QwtPlot *plot )
  : QObject( plot )
  , mPlot( plot )
{
}

bool QgsCurveEditorPlotEventFilter::eventFilter( QObject *, QEvent * ) { return false; }
QPointF QgsCurveEditorPlotEventFilter::mapPoint( QPointF point ) const { return point; }

// --- QgsGradientColorRampDialog ---
#include "qgsgradientcolorrampdialog.h"

const QgsSettingsEntryBool *QgsGradientColorRampDialog::settingsPlotHue = nullptr;
const QgsSettingsEntryBool *QgsGradientColorRampDialog::settingsPlotLightness = nullptr;
const QgsSettingsEntryBool *QgsGradientColorRampDialog::settingsPlotSaturation = nullptr;
const QgsSettingsEntryBool *QgsGradientColorRampDialog::settingsPlotAlpha = nullptr;

QgsGradientColorRampDialog::QgsGradientColorRampDialog( const QgsGradientColorRamp &ramp, QWidget *parent )
  : QDialog( parent )
  , mRamp( ramp )
{
  setLayout( new QVBoxLayout( this ) );
}

QgsGradientColorRampDialog::~QgsGradientColorRampDialog() = default;

void QgsGradientColorRampDialog::setRamp( const QgsGradientColorRamp &ramp ) { mRamp = ramp; emit changed(); }
QDialogButtonBox *QgsGradientColorRampDialog::buttonBox() const { return nullptr; }

// --- QgsRichTextEditor ---
#include "qgsrichtexteditor.h"
#include "qgsimagedroptextedit.h"
#include <QVBoxLayout>

QgsRichTextEditor::QgsRichTextEditor( QWidget *parent )
  : QWidget( parent )
{
  auto *l = new QVBoxLayout( this );
  l->setContentsMargins( 0, 0, 0, 0 );
  mTextEdit = new QgsImageDropTextEdit( this );
  l->addWidget( mTextEdit );
  connect( mTextEdit, &QTextEdit::textChanged, this, &QgsRichTextEditor::textChanged );
}

void QgsRichTextEditor::setMode( Mode mode ) { mMode = mode; }
QString QgsRichTextEditor::toPlainText() const { return mTextEdit ? mTextEdit->toPlainText() : QString(); }
QString QgsRichTextEditor::toHtml() const { return mTextEdit ? mTextEdit->toHtml() : QString(); }
void QgsRichTextEditor::setText( const QString &text ) { if ( mTextEdit ) mTextEdit->setText( text ); }
void QgsRichTextEditor::clearSource() { if ( mTextEdit ) mTextEdit->clear(); }
void QgsRichTextEditor::focusInEvent( QFocusEvent *e ) { QWidget::focusInEvent( e ); }
void QgsRichTextEditor::textRemoveFormat() {}
void QgsRichTextEditor::textRemoveAllFormat() {}
void QgsRichTextEditor::textBold() {}
void QgsRichTextEditor::textUnderline() {}
void QgsRichTextEditor::textStrikeout() {}
void QgsRichTextEditor::textItalic() {}
void QgsRichTextEditor::textSize( const QString & ) {}
void QgsRichTextEditor::textLink( bool ) {}
void QgsRichTextEditor::textStyle( int ) {}
void QgsRichTextEditor::textFgColor() {}
void QgsRichTextEditor::textBgColor() {}
void QgsRichTextEditor::listBullet( bool ) {}
void QgsRichTextEditor::listOrdered( bool ) {}
void QgsRichTextEditor::slotCurrentCharFormatChanged( const QTextCharFormat & ) {}
void QgsRichTextEditor::slotCursorPositionChanged() {}
void QgsRichTextEditor::slotClipboardDataChanged() {}
void QgsRichTextEditor::increaseIndentation() {}
void QgsRichTextEditor::decreaseIndentation() {}
void QgsRichTextEditor::insertImage() {}
void QgsRichTextEditor::editSource( bool ) {}
void QgsRichTextEditor::mergeFormatOnWordOrSelection( const QTextCharFormat & ) {}
void QgsRichTextEditor::fontChanged( const QFont & ) {}
void QgsRichTextEditor::fgColorChanged( const QColor & ) {}
void QgsRichTextEditor::bgColorChanged( const QColor & ) {}
void QgsRichTextEditor::list( bool, QTextListFormat::Style ) {}
void QgsRichTextEditor::indent( int ) {}

// --- QgsJsonEditWidget ---
#include "editorwidgets/qgsjsoneditwidget.h"

QgsJsonEditWidget::QgsJsonEditWidget( QWidget *parent )
  : QWidget( parent )
{
  setLayout( new QVBoxLayout( this ) );
}

QgsCodeEditorJson *QgsJsonEditWidget::jsonEditor() { return nullptr; }
void QgsJsonEditWidget::setJsonText( const QString &jsonText ) { mJsonText = jsonText; }
QString QgsJsonEditWidget::jsonText() const { return mJsonText; }
void QgsJsonEditWidget::setView( View ) const {}
void QgsJsonEditWidget::setFormatJsonMode( FormatJson mode ) { mFormatJsonMode = mode; }
void QgsJsonEditWidget::setControlsVisible( bool ) {}
void QgsJsonEditWidget::textToolButtonClicked( bool ) {}
void QgsJsonEditWidget::treeToolButtonClicked( bool ) {}
void QgsJsonEditWidget::copyValueActionTriggered() {}
void QgsJsonEditWidget::copyKeyActionTriggered() {}
void QgsJsonEditWidget::codeEditorJsonTextChanged() {}
void QgsJsonEditWidget::codeEditorJsonIndicatorClicked( int, int, Qt::KeyboardModifiers ) {}
void QgsJsonEditWidget::codeEditorJsonDwellStart( int, int, int ) {}
void QgsJsonEditWidget::codeEditorJsonDwellEnd( int, int, int ) {}
void QgsJsonEditWidget::refreshTreeView( const QJsonDocument & ) {}
void QgsJsonEditWidget::refreshTreeViewItem( QTreeWidgetItem *, const QJsonValue & ) {}
void QgsJsonEditWidget::refreshTreeViewItemValue( QTreeWidgetItem *, const QString &, const QColor & ) {}
QFont QgsJsonEditWidget::monospaceFont() const { return font(); }

// --- QgsSensorThingsSubsetEditor ---
#include "providers/sensorthings/qgssensorthingssubseteditor.h"

QgsSensorThingsSubsetEditor::QgsSensorThingsSubsetEditor( QgsVectorLayer *layer, const QgsFields &fields, QWidget *parent, Qt::WindowFlags fl )
  : QgsSubsetStringEditorInterface( parent, fl )
  , mLayer( layer )
  , mFields( fields )
{
  setLayout( new QVBoxLayout( this ) );
}

QString QgsSensorThingsSubsetEditor::subsetString() const { return property( "_qgs_subset" ).toString(); }
void QgsSensorThingsSubsetEditor::setSubsetString( const QString &subsetString ) { setProperty( "_qgs_subset", subsetString ); }
void QgsSensorThingsSubsetEditor::accept() { QDialog::accept(); }
void QgsSensorThingsSubsetEditor::reset() {}
void QgsSensorThingsSubsetEditor::lstFieldsDoubleClicked( const QModelIndex & ) {}

// --- QgsLayoutHtmlWidget ---
#include "layout/qgslayouthtmlwidget.h"
#include "layout/qgslayoutframe.h"
#include "layout/qgslayoutitemhtml.h"

QgsLayoutHtmlWidget::QgsLayoutHtmlWidget( QgsLayoutFrame *frame )
  : QgsLayoutItemBaseWidget( nullptr, nullptr )
  , mFrame( frame )
{
  setLayout( new QVBoxLayout( this ) );
}

void QgsLayoutHtmlWidget::setMasterLayout( QgsMasterLayoutInterface * ) {}
bool QgsLayoutHtmlWidget::setNewItem( QgsLayoutItem * ) { return true; }
void QgsLayoutHtmlWidget::mUrlLineEdit_editingFinished() {}
void QgsLayoutHtmlWidget::mFileToolButton_clicked() {}
void QgsLayoutHtmlWidget::mResizeModeComboBox_currentIndexChanged( int ) {}
void QgsLayoutHtmlWidget::mEvaluateExpressionsCheckbox_toggled( bool ) {}
void QgsLayoutHtmlWidget::mUseSmartBreaksCheckBox_toggled( bool ) {}
void QgsLayoutHtmlWidget::mMaxDistanceSpinBox_valueChanged( double ) {}
void QgsLayoutHtmlWidget::htmlEditorChanged() {}
void QgsLayoutHtmlWidget::stylesheetEditorChanged() {}
void QgsLayoutHtmlWidget::mUserStylesheetCheckBox_toggled( bool ) {}
void QgsLayoutHtmlWidget::mRadioManualSource_clicked( bool ) {}
void QgsLayoutHtmlWidget::mRadioUrlSource_clicked( bool ) {}
void QgsLayoutHtmlWidget::mInsertExpressionButton_clicked() {}
void QgsLayoutHtmlWidget::mReloadPushButton_clicked() {}
void QgsLayoutHtmlWidget::mAddFramePushButton_clicked() {}
void QgsLayoutHtmlWidget::mEmptyFrameCheckBox_toggled( bool ) {}
void QgsLayoutHtmlWidget::mHideEmptyBgCheckBox_toggled( bool ) {}
void QgsLayoutHtmlWidget::setGuiElementValues() {}
void QgsLayoutHtmlWidget::populateDataDefinedButtons() {}
void QgsLayoutHtmlWidget::blockSignals( bool ) {}

// --- QgsExpressionFinder ---
#include "qgsexpressionfinder.h"
#include <QPlainTextEdit>
#include <QTextEdit>

void QgsExpressionFinder::findExpressionAtPos( const QString &, int, int, int &, int &, QString &, const QString & ) {}
QString QgsExpressionFinder::findAndSelectActiveExpression( QgsCodeEditor *, const QString & ) { return QString(); }
QString QgsExpressionFinder::findAndSelectActiveExpression( QTextEdit *, const QString & ) { return QString(); }
QString QgsExpressionFinder::findAndSelectActiveExpression( QPlainTextEdit *, const QString & ) { return QString(); }

// --- QgsExpressionBuilderWidget additional stubs ---
void QgsExpressionBuilderWidget::showEvent( QShowEvent *e ) { QWidget::showEvent( e ); }
bool QgsExpressionBuilderWidget::parserError() const { return false; }
const QList<QgsExpressionItem *> QgsExpressionBuilderWidget::findExpressions( const QString & ) { return {}; }
void QgsExpressionBuilderWidget::autosave() {}
void QgsExpressionBuilderWidget::editSelectedUserExpression() {}
void QgsExpressionBuilderWidget::loadAllUsedValues() {}
void QgsExpressionBuilderWidget::loadAllValues() {}
void QgsExpressionBuilderWidget::loadSampleUsedValues() {}
void QgsExpressionBuilderWidget::loadSampleValues() {}
void QgsExpressionBuilderWidget::removeSelectedUserExpression() {}
void QgsExpressionBuilderWidget::storeCurrentUserExpression() {}
void QgsExpressionBuilderWidget::btnNewFile_pressed() {}
void QgsExpressionBuilderWidget::btnRemoveFile_pressed() {}
void QgsExpressionBuilderWidget::btnRun_pressed() {}
void QgsExpressionBuilderWidget::cmbFileNames_currentItemChanged( QListWidgetItem *, QListWidgetItem * ) {}
void QgsExpressionBuilderWidget::commentLinesClicked() {}
void QgsExpressionBuilderWidget::exportUserExpressions_pressed() {}
void QgsExpressionBuilderWidget::expressionTreeItemChanged( QgsExpressionItem * ) {}
void QgsExpressionBuilderWidget::importUserExpressions_pressed() {}
void QgsExpressionBuilderWidget::indicatorClicked( int, int, Qt::KeyboardModifiers ) {}
void QgsExpressionBuilderWidget::insertExpressionText( const QString & ) {}
void QgsExpressionBuilderWidget::mValuesListView_doubleClicked( const QModelIndex & ) {}
void QgsExpressionBuilderWidget::onExpressionParsed( bool ) {}
void QgsExpressionBuilderWidget::operatorButtonClicked() {}
void QgsExpressionBuilderWidget::txtExpressionString_textChanged() {}
void QgsExpressionBuilderWidget::txtPython_textChanged() {}
void QgsExpressionBuilderWidget::txtSearchEditValues_textChanged() {}

// --- QgsGradientColorRampDialog additional stubs ---
void QgsGradientColorRampDialog::setColor1( const QColor & ) {}
void QgsGradientColorRampDialog::setColor2( const QColor & ) {}
void QgsGradientColorRampDialog::updateColorButtons() {}
void QgsGradientColorRampDialog::updateRampFromStopEditor() {}
void QgsGradientColorRampDialog::updateStopEditor() {}
void QgsGradientColorRampDialog::btnInformation_pressed() {}
void QgsGradientColorRampDialog::cboType_currentIndexChanged( int ) {}
void QgsGradientColorRampDialog::colorWidgetChanged( const QColor & ) {}
void QgsGradientColorRampDialog::mPlotAlphaCheckbox_toggled( bool ) {}
void QgsGradientColorRampDialog::mPlotHueCheckbox_toggled( bool ) {}
void QgsGradientColorRampDialog::mPlotLightnessCheckbox_toggled( bool ) {}
void QgsGradientColorRampDialog::mPlotSaturationCheckbox_toggled( bool ) {}
void QgsGradientColorRampDialog::mPositionSpinBox_valueChanged( double ) {}
void QgsGradientColorRampDialog::plotMouseMove( QPointF ) {}
void QgsGradientColorRampDialog::plotMousePress( QPointF ) {}
void QgsGradientColorRampDialog::plotMouseRelease( QPointF ) {}
void QgsGradientColorRampDialog::selectedStopChanged( const QgsGradientStop & ) {}
void QgsGradientColorRampDialog::showHelp() {}

// --- QgsRasterHistogramWidget ---
#include "raster/qgsrasterhistogramwidget.h"

QgsRasterHistogramWidget::QgsRasterHistogramWidget( QgsRasterLayer *layer, QWidget *parent )
  : QgsMapLayerConfigWidget( layer, nullptr, parent )
{
  setLayout( new QVBoxLayout( this ) );
}
bool QgsRasterHistogramWidget::histoSaveAsImage( const QString &, int, int, int ) { return false; }
void QgsRasterHistogramWidget::setRendererWidget( const QString &, QgsRasterRendererWidget * ) {}
void QgsRasterHistogramWidget::setActive( bool ) {}
bool QgsRasterHistogramWidget::computeHistogram( bool ) { return false; }
void QgsRasterHistogramWidget::histoAction( const QString &, bool ) {}
void QgsRasterHistogramWidget::setSelectedBand( int ) {}
void QgsRasterHistogramWidget::refreshHistogram() {}
void QgsRasterHistogramWidget::apply() {}
void QgsRasterHistogramWidget::mSaveAsImageButton_clicked() {}
void QgsRasterHistogramWidget::cboHistoBand_currentIndexChanged( int ) {}
void QgsRasterHistogramWidget::applyHistoMin() {}
void QgsRasterHistogramWidget::applyHistoMax() {}
void QgsRasterHistogramWidget::btnHistoMin_toggled() {}
void QgsRasterHistogramWidget::btnHistoMax_toggled() {}
void QgsRasterHistogramWidget::histoPickerSelected( QPointF ) {}
void QgsRasterHistogramWidget::histoPickerSelectedQwt5( QwtDoublePoint ) {}
void QgsRasterHistogramWidget::histoActionTriggered( QAction * ) {}
void QgsRasterHistogramWidget::updateHistoMarkers() {}
void QgsRasterHistogramWidget::btnHistoCompute_clicked() {}

// --- QgsSQLComposerDialog ---
#include "qgssqlcomposerdialog.h"

QgsSQLComposerDialog::QgsSQLComposerDialog( QWidget *parent, Qt::WindowFlags fl )
  : QgsSubsetStringEditorInterface( parent, fl )
{
  setLayout( new QVBoxLayout( this ) );
}
QgsSQLComposerDialog::~QgsSQLComposerDialog() = default;
QString QgsSQLComposerDialog::sql() const { return QString(); }
void QgsSQLComposerDialog::setSql( const QString & ) {}
void QgsSQLComposerDialog::buildSQLFromFields() {}
void QgsSQLComposerDialog::mAddJoinButton_clicked() {}
void QgsSQLComposerDialog::mColumnsCombo_currentIndexChanged( int ) {}
void QgsSQLComposerDialog::mFunctionsCombo_currentIndexChanged( int ) {}
void QgsSQLComposerDialog::mOperatorsCombo_currentIndexChanged( int ) {}
void QgsSQLComposerDialog::mRemoveJoinButton_clicked() {}
void QgsSQLComposerDialog::mSpatialPredicatesCombo_currentIndexChanged( int ) {}
void QgsSQLComposerDialog::mTableJoins_itemSelectionChanged() {}
void QgsSQLComposerDialog::mTablesCombo_currentIndexChanged( int ) {}
void QgsSQLComposerDialog::reset() {}
void QgsSQLComposerDialog::showHelp() {}
void QgsSQLComposerDialog::splitSQLIntoFields() {}
bool QgsSQLComposerDialog::eventFilter( QObject *o, QEvent *e ) { return QDialog::eventFilter( o, e ); }
void QgsSQLComposerDialog::accept() { QDialog::accept(); }

// --- QgsQueryResultWidget & dialogs ---
#include "qgsqueryresultwidget.h"

const QgsSettingsEntryString *QgsQueryResultWidget::settingLastSourceFolder = nullptr;

QgsQueryResultItemDelegate::QgsQueryResultItemDelegate( QObject *parent )
  : QStyledItemDelegate( parent ) {}
QString QgsQueryResultItemDelegate::displayText( const QVariant &value, const QLocale &locale ) const
{
  return QStyledItemDelegate::displayText( value, locale );
}

QgsQueryResultWidget::QgsQueryResultWidget( QWidget *parent, QgsAbstractDatabaseProviderConnection * )
  : QWidget( parent )
{
  setLayout( new QVBoxLayout( this ) );
}
QgsQueryResultWidget::~QgsQueryResultWidget() = default;
void QgsQueryResultWidget::setSqlVectorLayerOptions( const QgsAbstractDatabaseProviderConnection::SqlVectorLayerOptions & ) {}
void QgsQueryResultWidget::setWidgetMode( QueryWidgetMode ) {}
void QgsQueryResultWidget::setConnection( QgsAbstractDatabaseProviderConnection * ) {}
void QgsQueryResultWidget::setQuery( const QString & ) {}
bool QgsQueryResultWidget::promptUnsavedChanges() { return true; }
void QgsQueryResultWidget::notify( const QString &, const QString &, Qgis::MessageLevel ) {}
void QgsQueryResultWidget::executeQuery() {}
void QgsQueryResultWidget::showError( const QString &, const QString &, bool ) {}
void QgsQueryResultWidget::tokensReady( const QStringList & ) {}
void QgsQueryResultWidget::copyResults() {}
void QgsQueryResultWidget::copyResults( int, int, int, int ) {}
void QgsQueryResultWidget::openQuery() {}
void QgsQueryResultWidget::populatePresetQueryMenu() {}
void QgsQueryResultWidget::saveQuery( bool ) {}
void QgsQueryResultWidget::setHasChanged( bool ) {}
void QgsQueryResultWidget::showHistoryPanel( bool ) {}
void QgsQueryResultWidget::updateButtons() {}

QgsQueryResultPanelWidget::QgsQueryResultPanelWidget( QWidget *parent, QgsAbstractDatabaseProviderConnection * )
  : QgsPanelWidget( parent )
{
  setLayout( new QVBoxLayout( this ) );
}
QgsQueryResultPanelWidget::~QgsQueryResultPanelWidget() = default;
QgsCodeEditorSQL *QgsQueryResultPanelWidget::sqlEditor() { return nullptr; }
QgsCodeEditorWidget *QgsQueryResultPanelWidget::codeEditorWidget() { return nullptr; }
void QgsQueryResultPanelWidget::copySelection() {}
void QgsQueryResultPanelWidget::showCellContextMenu( QPoint ) {}
void QgsQueryResultPanelWidget::updateButtons() {}
void QgsQueryResultPanelWidget::copyResults( int, int, int, int ) {}
void QgsQueryResultPanelWidget::copyResults() {}
void QgsQueryResultPanelWidget::executeQuery() {}
void QgsQueryResultPanelWidget::notify( const QString &, const QString &, Qgis::MessageLevel ) {}
void QgsQueryResultPanelWidget::showError( const QString &, const QString &, bool ) {}
void QgsQueryResultPanelWidget::tokensReady( const QStringList & ) {}
void QgsQueryResultPanelWidget::setWidgetMode( QgsQueryResultWidget::QueryWidgetMode ) {}
void QgsQueryResultPanelWidget::setConnection( QgsAbstractDatabaseProviderConnection * ) {}
void QgsQueryResultPanelWidget::setSqlVectorLayerOptions( const QgsAbstractDatabaseProviderConnection::SqlVectorLayerOptions & ) {}
void QgsQueryResultPanelWidget::setQuery( const QString & ) {}

QgsQueryResultDialog::QgsQueryResultDialog( QgsAbstractDatabaseProviderConnection *, QWidget *parent )
  : QDialog( parent )
{
  setLayout( new QVBoxLayout( this ) );
}
void QgsQueryResultDialog::closeEvent( QCloseEvent *e ) { QDialog::closeEvent( e ); }

QgsQueryResultMainWindow::QgsQueryResultMainWindow( QgsAbstractDatabaseProviderConnection *, const QString & )
{
}
void QgsQueryResultMainWindow::closeEvent( QCloseEvent *e ) { QMainWindow::closeEvent( e ); }

// --- History Providers ---
#include "qgsdbqueryhistoryprovider.h"
#include "processing/qgsprocessinghistoryprovider.h"

QgsDatabaseQueryHistoryProvider::QgsDatabaseQueryHistoryProvider() = default;
QString QgsDatabaseQueryHistoryProvider::id() const { return QStringLiteral( "dbquery" ); }
QgsHistoryEntryNode *QgsDatabaseQueryHistoryProvider::createNodeForEntry( const QgsHistoryEntry &, const QgsHistoryWidgetContext & ) { return nullptr; }
void QgsDatabaseQueryHistoryProvider::updateNodeForEntry( QgsHistoryEntryNode *, const QgsHistoryEntry &, const QgsHistoryWidgetContext & ) {}

QgsDatabaseQueryHistoryWidget::QgsDatabaseQueryHistoryWidget( Qgis::HistoryProviderBackends backends, QgsHistoryProviderRegistry *registry, const QgsHistoryWidgetContext &context, QWidget *parent )
  : QgsHistoryWidget( QStringLiteral( "dbquery" ), backends, registry, context, parent )
{
}
void QgsDatabaseQueryHistoryWidget::emitSqlTriggered( const QString &, const QString &, const QString & ) {}

QgsProcessingHistoryProvider::QgsProcessingHistoryProvider() = default;
QString QgsProcessingHistoryProvider::id() const { return QStringLiteral( "processing" ); }
void QgsProcessingHistoryProvider::portOldLog() {}
QgsHistoryEntryNode *QgsProcessingHistoryProvider::createNodeForEntry( const QgsHistoryEntry &, const QgsHistoryWidgetContext & ) { return nullptr; }
void QgsProcessingHistoryProvider::updateNodeForEntry( QgsHistoryEntryNode *, const QgsHistoryEntry &, const QgsHistoryWidgetContext & ) {}



