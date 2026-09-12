/***************************************************************************
 * schema_form_builder.cpp  —  schema JSON → validated Qt form widgets
 ***************************************************************************/
#include "schema_form_builder.h"

#include "help/help_id.h"
#include "help/help_presenter.h"
#include "help/help_registry.h"
#include "widgets/crs_selector.h"
#include "widgets/rs_scan_pool.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QThreadPool>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace {

bool isTruthyJson( const Json::Value &v )
{
  if ( v.isBool() )
    return v.asBool();
  if ( v.isString() )
  {
    const QString s = QString::fromStdString( v.asString() ).trimmed().toLower();
    return s == QLatin1String( "true" ) || s == QLatin1String( "1" ) || s == QLatin1String( "yes" );
  }
  if ( v.isNumeric() )
    return v.asDouble() != 0.0;
  return false;
}

QString memberString( const Json::Value &obj, const char *key )
{
  if ( !obj.isObject() || !obj.isMember( key ) )
    return {};
  const Json::Value &v = obj[key];
  if ( v.isString() )
    return QString::fromStdString( v.asString() );
  return {};
}

int uiOrder( const Json::Value &prop )
{
  if ( prop.isObject() && prop.isMember( "x-ui-order" ) && prop["x-ui-order"].isNumeric() )
    return prop["x-ui-order"].asInt();
  return 1000;
}

bool nameLooksLikeInput( const QString &name )
{
  const QString n = name.toLower();
  static const char *const kKeys[] = {
    "input", "before", "after", "pan", "ms", "inputs", "raster", "source", "in_"
  };
  for ( const char *k : kKeys )
  {
    if ( n == QLatin1String( k ) || n.startsWith( QLatin1String( k ) ) )
      return true;
  }
  return false;
}

bool nameLooksLikeOutput( const QString &name )
{
  const QString n = name.toLower();
  return n == QLatin1String( "output" )
         || n.startsWith( QLatin1String( "output" ) )
         || n == QLatin1String( "out" )
         || n.startsWith( QLatin1String( "out_" ) );
}

bool isRasterFormat( const Json::Value &prop )
{
  const QString format = memberString( prop, "format" ).toLower();
  if ( format == QLatin1String( "raster" )
       || format == QLatin1String( "tif" )
       || format == QLatin1String( "tiff" )
       || format == QLatin1String( "geotiff" )
       || format == QLatin1String( "gtiff" ) )
    return true;

  const QString widget = memberString( prop, "x-ui-widget" ).toLower();
  if ( widget == QLatin1String( "layer-raster" ) )
    return true;

  return false;
}

bool isOutputRole( const Json::Value &prop )
{
  const QString role = memberString( prop, "SicnuFileRole" ).toLower();
  if ( role == QLatin1String( "output" ) )
    return true;
  const QString group = memberString( prop, "x-ui-group" ).toLower();
  return group == QLatin1String( "output" );
}

bool isInputRole( const Json::Value &prop )
{
  const QString role = memberString( prop, "SicnuFileRole" ).toLower();
  if ( role == QLatin1String( "input" ) )
    return true;
  const QString group = memberString( prop, "x-ui-group" ).toLower();
  return group == QLatin1String( "input" );
}

const Json::Value *propertiesObject( const Json::Value &schema )
{
  if ( !schema.isObject() )
    return nullptr;
  if ( schema.isMember( "properties" ) && schema["properties"].isObject() )
    return &schema["properties"];
  // Allow a bare params object (no JSON-Schema wrapper).
  return &schema;
}

/// The "required" name list of one object schema (empty when absent).
QStringList requiredListOf( const Json::Value &objectProp )
{
  QStringList out;
  if ( objectProp.isObject() && objectProp.isMember( "required" )
       && objectProp["required"].isArray() )
  {
    for ( const Json::Value &r : objectProp["required"] )
    {
      if ( r.isString() )
        out << QString::fromStdString( r.asString() );
    }
  }
  return out;
}

/// Whether the schema declares a numeric bound for object arrays.
int maxItemsOf( const Json::Value &prop )
{
  if ( prop.isObject() && prop.isMember( "maxItems" ) && prop["maxItems"].isNumeric() )
    return prop["maxItems"].asInt();
  return -1;
}

int minItemsOf( const Json::Value &prop )
{
  if ( prop.isObject() && prop.isMember( "minItems" ) && prop["minItems"].isNumeric() )
    return prop["minItems"].asInt();
  return 0;
}

QGroupBox *makeSectionBox( QWidget *parent, const QString &title, bool advanced )
{
  auto *box = new QGroupBox( title, parent );
  if ( advanced )
  {
    box->setCheckable( true );
    box->setChecked( false );
    box->setFlat( false );
  }
  auto *layout = new QFormLayout( box );
  layout->setContentsMargins( 8, 12, 8, 8 );
  layout->setHorizontalSpacing( 12 );
  layout->setVerticalSpacing( 8 );
  return box;
}

QLabel *makeSectionLabel( QWidget *parent, const QString &text )
{
  auto *lab = new QLabel( text, parent );
  lab->setObjectName( QStringLiteral( "rsTaskPanelSectionLabel" ) );
  QFont f = lab->font();
  f.setBold( true );
  lab->setFont( f );
  return lab;
}

/// Async-check names supported by the form. Unknown check ids are ignored at
/// schedule time (the schema vocabulary is versioned; unknown never crashes).
QStringList knownChecks( const QStringList &raw )
{
  QStringList out;
  for ( const QString &c : raw )
  {
    if ( c == QLatin1String( "path_exists" ) )
      out << c;
  }
  return out;
}

} // namespace

SchemaFormBuilder::SchemaFormBuilder( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "schemaFormBuilder" ) );
  m_root = new QVBoxLayout( this );
  m_root->setContentsMargins( 0, 0, 0, 0 );
  m_root->setSpacing( 10 );

  // Live validation: every value change re-runs the schema checks and
  // refreshes the inline marks + summary line.
  connect( this, &SchemaFormBuilder::valuesChanged,
           this, &SchemaFormBuilder::updateValidationUi );

  // Async x-ui-check runs are debounced: a burst of edits (spin box arrows)
  // costs one pool dispatch after the user pauses, not one per keystroke.
  m_checkDebounce = new QTimer( this );
  m_checkDebounce->setSingleShot( true );
  m_checkDebounce->setInterval( 350 );
  connect( m_checkDebounce, &QTimer::timeout,
           this, &SchemaFormBuilder::scheduleAsyncChecks );
  connect( this, &SchemaFormBuilder::valuesChanged,
           m_checkDebounce, QOverload<>::of( &QTimer::start ) );
}

SchemaFormBuilder::~SchemaFormBuilder()
{
  // In-flight pool jobs deliver through QPointer + invokeMethod(context) and
  // drop once this object dies; bumping the generation also orphans any
  // result that races the destruction window.
  ++m_checkGeneration;
}

void SchemaFormBuilder::setHelpContext( const QString &operatorId )
{
  m_helpOperatorId = operatorId;
}

void SchemaFormBuilder::clearFields()
{
  m_fields.clear();
  m_validationLabel = nullptr; // deleteLater'd with the layout below
  while ( QLayoutItem *item = m_root->takeAt( 0 ) )
  {
    if ( QWidget *w = item->widget() )
      w->deleteLater();
    delete item;
  }
}

SchemaFormBuilder::FieldGroup
SchemaFormBuilder::classifyGroup( const QString &name, const Json::Value &prop )
{
  if ( prop.isObject() && prop.isMember( "x-ui-advanced" ) && isTruthyJson( prop["x-ui-advanced"] ) )
    return FieldGroup::Advanced;

  const QString group = memberString( prop, "x-ui-group" ).toLower();
  if ( group == QLatin1String( "input" ) )
    return FieldGroup::Input;
  if ( group == QLatin1String( "output" ) )
    return FieldGroup::Output;
  if ( group == QLatin1String( "params" ) || group == QLatin1String( "parameters" ) )
    return FieldGroup::Params;

  if ( isOutputRole( prop ) || nameLooksLikeOutput( name ) )
    return FieldGroup::Output;

  if ( isInputRole( prop ) || nameLooksLikeInput( name ) || isRasterFormat( prop ) )
  {
    // Raster output params have format tif + SicnuFileRole output — already handled.
    if ( !isOutputRole( prop ) && !nameLooksLikeOutput( name ) )
      return FieldGroup::Input;
  }

  // makeRasterParam uses format=raster + SicnuFileRole=input
  if ( isRasterFormat( prop ) && !isOutputRole( prop ) )
    return FieldGroup::Input;

  return FieldGroup::Params;
}

SchemaFormBuilder::FieldKind
SchemaFormBuilder::classifyKind( const QString &name, const Json::Value &prop, int depth )
{
  // x-ui-type hints (AlgorithmDescriptor ports emit raster/vector/table/
  // bbox/crs; workflows may add asset/model/color/expression/json) — the
  // authoritative mapping from data contract to editor kind.
  const QString uiType = memberString( prop, "x-ui-type" ).toLower();
  if ( uiType == QLatin1String( "raster" ) )
    return FieldKind::RasterCombo;
  if ( uiType == QLatin1String( "vector" ) || uiType == QLatin1String( "table" ) )
    return FieldKind::VectorCombo;
  if ( uiType == QLatin1String( "crs" ) )
    return FieldKind::Crs;
  if ( uiType == QLatin1String( "bbox" ) )
    return FieldKind::Array;
  if ( uiType == QLatin1String( "asset" ) )
    return FieldKind::AssetCombo;
  if ( uiType == QLatin1String( "model" ) )
    return FieldKind::ModelCombo;
  if ( uiType == QLatin1String( "color" ) )
    return FieldKind::Color;
  if ( uiType == QLatin1String( "expression" ) )
    return FieldKind::String;
  if ( uiType == QLatin1String( "json" ) )
    return FieldKind::Json;

  // SchemaForm 4.0: structural detection (schema shape wins over widget
  // hints). Depth-capped: deeper object schemas degrade to the JSON editor.
  if ( depth < kMaxObjectDepth )
  {
    const QString typeStruct = memberString( prop, "type" ).toLower();
    if ( typeStruct == QLatin1String( "object" )
         && prop.isObject() && prop.isMember( "properties" )
         && prop["properties"].isObject() )
      return FieldKind::Object;

    if ( typeStruct == QLatin1String( "array" ) && prop.isObject()
         && prop.isMember( "items" ) && prop["items"].isObject() )
    {
      const Json::Value &items = prop["items"];
      const QString itemType = memberString( items, "type" ).toLower();
      const bool objectItems =
        ( itemType == QLatin1String( "object" ) || itemType.isEmpty() )
        && items.isMember( "properties" ) && items["properties"].isObject();
      if ( objectItems )
        return FieldKind::ObjectArray;
    }
  }

  const QString widget = memberString( prop, "x-ui-widget" ).toLower();
  if ( widget == QLatin1String( "array" ) )
    return FieldKind::Array;
  // JSON Schema type:"array" must be handled before string fallback.
  {
    const QString typeEarly = memberString( prop, "type" ).toLower();
    if ( typeEarly == QLatin1String( "array" ) )
      return FieldKind::Array;
  }
  if ( widget == QLatin1String( "layer-raster" ) )
    return FieldKind::RasterCombo;
  if ( widget == QLatin1String( "layer-vector" ) )
    return FieldKind::VectorCombo;
  if ( widget == QLatin1String( "asset" ) )
    return FieldKind::AssetCombo;
  if ( widget == QLatin1String( "model" ) )
    return FieldKind::ModelCombo;
  if ( widget == QLatin1String( "crs" ) )
    return FieldKind::Crs;
  if ( widget == QLatin1String( "color" ) )
    return FieldKind::Color;
  if ( widget == QLatin1String( "json" ) )
    return FieldKind::Json;
  if ( widget == QLatin1String( "enum" ) )
    return FieldKind::Enum;
  if ( widget == QLatin1String( "number" ) )
    return FieldKind::Double;
  if ( widget == QLatin1String( "bool" ) || widget == QLatin1String( "boolean" ) )
    return FieldKind::Boolean;
  if ( widget == QLatin1String( "string" ) )
    return FieldKind::String;
  if ( widget == QLatin1String( "file" ) )
  {
    if ( isOutputRole( prop ) || nameLooksLikeOutput( name ) )
      return FieldKind::OutputPath;
    return FieldKind::String;
  }

  if ( prop.isObject() && prop.isMember( "enum" ) && prop["enum"].isArray() )
    return FieldKind::Enum;
  // SchemaForm 4.0: a declared dynamic enum source is an enum editor even
  // without a static enum list (choices come from the provider).
  if ( !memberString( prop, "x-ui-enum-source" ).isEmpty() )
    return FieldKind::Enum;

  const QString type = memberString( prop, "type" ).toLower();
  if ( type == QLatin1String( "boolean" ) )
    return FieldKind::Boolean;
  if ( type == QLatin1String( "integer" ) )
    return FieldKind::Integer;
  if ( type == QLatin1String( "number" ) )
    return FieldKind::Double;
  if ( type == QLatin1String( "object" ) )
    return FieldKind::Json;

  if ( isOutputRole( prop ) || nameLooksLikeOutput( name ) )
    return FieldKind::OutputPath;

  // Raster inputs / layer picks
  if ( isRasterFormat( prop )
       || widget == QLatin1String( "layer-raster" )
       || nameLooksLikeInput( name ) )
  {
    // "input" style names that are not output
    if ( !nameLooksLikeOutput( name ) && !isOutputRole( prop ) )
    {
      const QString format = memberString( prop, "format" ).toLower();
      if ( format == QLatin1String( "vector" ) || widget == QLatin1String( "layer-vector" ) )
        return FieldKind::String; // path line for now
      return FieldKind::RasterCombo;
    }
  }

  if ( type == QLatin1String( "string" ) || type.isEmpty() )
  {
    // Output-looking formats
    const QString format = memberString( prop, "format" ).toLower();
    if ( format == QLatin1String( "tif" ) || format == QLatin1String( "tiff" )
         || format == QLatin1String( "geotiff" ) )
    {
      if ( isOutputRole( prop ) || nameLooksLikeOutput( name ) )
        return FieldKind::OutputPath;
    }
    return FieldKind::String;
  }

  return FieldKind::String;
}

QVector<QPair<QString, Json::Value>>
SchemaFormBuilder::orderedProperties( const Json::Value &objectProp )
{
  QVector<QPair<QString, Json::Value>> out;
  if ( !objectProp.isObject() || !objectProp.isMember( "properties" )
       || !objectProp["properties"].isObject() )
    return out;

  const Json::Value &props = objectProp["properties"];
  struct Entry
  {
    QString name;
    Json::Value prop;
    int order = 1000;
  };
  std::vector<Entry> entries;
  entries.reserve( props.getMemberNames().size() );
  for ( const std::string &key : props.getMemberNames() )
  {
    const Json::Value &prop = props[key];
    if ( !prop.isObject() )
      continue;
    Entry e;
    e.name = QString::fromStdString( key );
    const QString embedded = memberString( prop, "name" );
    if ( !embedded.isEmpty() )
      e.name = embedded;
    e.prop = prop;
    e.order = uiOrder( prop );
    entries.push_back( std::move( e ) );
  }
  std::stable_sort( entries.begin(), entries.end(),
                    []( const Entry &a, const Entry &b )
  {
    if ( a.order != b.order )
      return a.order < b.order;
    return a.name < b.name;
  } );

  out.reserve( static_cast<int>( entries.size() ) );
  for ( Entry &e : entries )
    out.append( { e.name, std::move( e.prop ) } );
  return out;
}

void SchemaFormBuilder::applyParameterHelp( Field &field, const QString &label )
{
  if ( m_helpOperatorId.isEmpty() )
    return;
  const QString helpId = sicnu::help::HelpId::parameterId( m_helpOperatorId, field.name );
  const sicnu::help::HelpDescriptor *d = sicnu::help::globalHelpRegistry().find( helpId );
  if ( !d )
    return;

  field.widget->setProperty( "helpId", helpId );

  QStringList tooltipLines;
  if ( !label.isEmpty() )
    tooltipLines << label;
  if ( d->parameter.has_value() )
  {
    const sicnu::help::ParameterKnowledge &knowledge = *d->parameter;
    if ( !knowledge.meaning.isEmpty() )
      tooltipLines << knowledge.meaning;
    if ( !knowledge.unit.isEmpty() )
      tooltipLines << tr( "单位：%1" ).arg( knowledge.unit );
    if ( !knowledge.recommended.isEmpty() )
      tooltipLines << tr( "推荐：%1" ).arg( knowledge.recommended );
    if ( !knowledge.tradeOff.isEmpty() )
      tooltipLines << tr( "权衡：%1" ).arg( knowledge.tradeOff );
    for ( const QString &warning : knowledge.warnings )
      tooltipLines << QStringLiteral( "⚠ %1" ).arg( warning );
  }
  else if ( !d->summary.isEmpty() )
  {
    tooltipLines << d->summary;
  }
  if ( tooltipLines.size() > 1 )
    field.widget->setToolTip( tooltipLines.join( QStringLiteral( "\n" ) ) );
  field.widget->setWhatsThis( sicnu::help::HelpPresenter::whatsThis( *d ) );
}

QString SchemaFormBuilder::fieldLabel( const QString &name, const Json::Value &prop )
{
  const QString title = memberString( prop, "title" );
  if ( !title.isEmpty() )
    return title;
  const QString desc = memberString( prop, "description" );
  if ( !desc.isEmpty() && desc.size() < 48 )
    return desc;
  return name;
}

QStringList SchemaFormBuilder::parseChecks( const Json::Value &prop )
{
  if ( !prop.isObject() || !prop.isMember( "x-ui-check" ) )
    return {};
  QStringList raw;
  const Json::Value &c = prop["x-ui-check"];
  if ( c.isString() )
    raw << QString::fromStdString( c.asString() );
  else if ( c.isArray() )
  {
    for ( const Json::Value &v : c )
    {
      if ( v.isString() )
        raw << QString::fromStdString( v.asString() );
    }
  }
  return knownChecks( raw );
}

SchemaFormBuilder::Field
SchemaFormBuilder::buildField( const QString &path, const Json::Value &prop, int depth )
{
  Field field;
  field.name = path.mid( path.lastIndexOf( QLatin1Char( '.' ) ) + 1 );
  field.path = path;
  field.depth = depth;
  field.kind = classifyKind( path.mid( path.lastIndexOf( QLatin1Char( '.' ) ) + 1 ), prop, depth );
  field.group = classifyGroup( field.name, prop );
  field.prop = prop;
  field.unit = memberString( prop, "x-ui-unit" );
  field.recommended = memberString( prop, "x-ui-recommended" );
  field.enumSource = memberString( prop, "x-ui-enum-source" );
  field.checks = parseChecks( prop );
  if ( prop.isObject() && prop.isMember( "x-ui-visible-when" )
       && prop["x-ui-visible-when"].isObject() )
    field.visibleWhen = prop["x-ui-visible-when"];

  const QString tip = memberString( prop, "description" );

  switch ( field.kind )
  {
    case FieldKind::RasterCombo:
    {
      auto *combo = new QComboBox( this );
      combo->setEditable( true );
      combo->setInsertPolicy( QComboBox::NoInsert );
      combo->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
      if ( !tip.isEmpty() )
        combo->setToolTip( tip );
      field.combo = combo;
      field.widget = combo;
      break;
    }
    case FieldKind::VectorCombo:
    {
      auto *combo = new QComboBox( this );
      combo->setEditable( true );
      combo->setInsertPolicy( QComboBox::NoInsert );
      combo->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
      if ( !tip.isEmpty() )
        combo->setToolTip( tip );
      field.combo = combo;
      field.widget = combo;
      break;
    }
    case FieldKind::AssetCombo:
    {
      // Governed Data Assets: stable ids as item data (context actions must
      // act on ids, never row positions).
      auto *combo = new QComboBox( this );
      combo->setEditable( false );
      combo->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
      combo->addItem( tr( "选择数据资产…" ), QString() );
      if ( !tip.isEmpty() )
        combo->setToolTip( tip );
      field.combo = combo;
      field.widget = combo;
      break;
    }
    case FieldKind::ModelCombo:
    {
      auto *combo = new QComboBox( this );
      combo->setEditable( false );
      combo->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
      combo->addItem( tr( "选择模型…" ), QString() );
      if ( !tip.isEmpty() )
        combo->setToolTip( tip );
      field.combo = combo;
      field.widget = combo;
      break;
    }
    case FieldKind::Crs:
    {
      auto *selector = new CrsSelector( this );
      if ( !tip.isEmpty() )
        selector->setToolTip( tip );
      if ( prop.isMember( "default" ) && prop["default"].isString() )
        selector->setCrsString( QString::fromStdString( prop["default"].asString() ) );
      field.crsSelector = selector;
      field.widget = selector;
      break;
    }
    case FieldKind::Json:
    {
      // Advanced JSON object editor (collapsed with the advanced section;
      // never invisible by contract).
      auto *edit = new QPlainTextEdit( this );
      edit->setMaximumHeight( 96 );
      QFont mono = edit->font();
      mono.setFamily( QStringLiteral( "IBM Plex Mono" ) );
      mono.setStyleHint( QFont::Monospace );
      edit->setFont( mono );
      edit->setPlaceholderText( tr( "{ \u2026 } JSON 对象" ) );
      if ( prop.isMember( "default" ) && prop["default"].isString() )
        edit->setPlainText( QString::fromStdString( prop["default"].asString() ) );
      if ( !tip.isEmpty() )
        edit->setToolTip( tip );
      field.plainEdit = edit;
      field.widget = edit;
      break;
    }
    case FieldKind::Color:
    {
      auto *row = new QWidget( this );
      auto *hl = new QHBoxLayout( row );
      hl->setContentsMargins( 0, 0, 0, 0 );
      hl->setSpacing( 6 );
      auto *edit = new QLineEdit( row );
      if ( prop.isMember( "default" ) && prop["default"].isString() )
        edit->setText( QString::fromStdString( prop["default"].asString() ) );
      edit->setPlaceholderText( QStringLiteral( "#RRGGBB" ) );
      auto *pick = new QPushButton( tr( "…" ), row );
      pick->setObjectName( QStringLiteral( "rsTaskPanelColorPick" ) );
      hl->addWidget( edit, 1 );
      hl->addWidget( pick );
      connect( pick, &QPushButton::clicked, this, [this, edit]()
      {
        const QColor chosen = QColorDialog::getColor(
          QColor( edit->text() ), this, tr( "选择颜色" ) );
        if ( chosen.isValid() )
        {
          edit->setText( chosen.name( QColor::HexRgb ) );
          emit valuesChanged();
        }
      } );
      field.lineEdit = edit;
      field.widget = row;
      break;
    }
    case FieldKind::OutputPath:
    {
      auto *row = new QWidget( this );
      auto *hl = new QHBoxLayout( row );
      hl->setContentsMargins( 0, 0, 0, 0 );
      hl->setSpacing( 6 );
      auto *edit = new QLineEdit( row );
      edit->setPlaceholderText( tr( "输出路径…" ) );
      if ( !tip.isEmpty() )
        edit->setToolTip( tip );
      auto *browse = new QPushButton( tr( "浏览…" ), row );
      browse->setObjectName( QStringLiteral( "rsTaskPanelBrowse" ) );
      hl->addWidget( edit, 1 );
      hl->addWidget( browse );
      connect( browse, &QPushButton::clicked, this, [this, edit]()
      {
        const QString path = QFileDialog::getSaveFileName(
          this,
          tr( "选择输出文件" ),
          edit->text(),
          tr( "GeoTIFF (*.tif *.tiff);;All Files (*)" ) );
        if ( !path.isEmpty() )
        {
          edit->setText( path );
          emit valuesChanged();
        }
      } );
      field.lineEdit = edit;
      field.widget = row;
      break;
    }
    case FieldKind::Enum:
    {
      auto *combo = new QComboBox( this );
      combo->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
      if ( prop.isMember( "enum" ) && prop["enum"].isArray() )
      {
        for ( const Json::Value &v : prop["enum"] )
        {
          if ( v.isString() )
            combo->addItem( QString::fromStdString( v.asString() ),
                            QString::fromStdString( v.asString() ) );
          else if ( v.isNumeric() )
            combo->addItem( QString::number( v.asDouble() ), v.asDouble() );
          else
            combo->addItem( QString::fromStdString( v.toStyledString().c_str() ) );
        }
      }
      else if ( !field.enumSource.isEmpty() )
      {
        // 4.0: dynamic source. Choices populate in refreshChoices(); until
        // then the combo stays editable so a missing provider never bricks
        // the parameter (honest degradation, documented in the header).
        combo->setEditable( true );
        combo->setInsertPolicy( QComboBox::NoInsert );
      }
      if ( prop.isMember( "default" ) )
      {
        if ( prop["default"].isString() )
        {
          const QString d = QString::fromStdString( prop["default"].asString() );
          const int idx = combo->findData( d );
          if ( idx >= 0 )
            combo->setCurrentIndex( idx );
          else
          {
            const int t = combo->findText( d );
            if ( t >= 0 )
              combo->setCurrentIndex( t );
          }
        }
        else if ( prop["default"].isNumeric() )
        {
          const double d = prop["default"].asDouble();
          const int idx = combo->findData( d );
          if ( idx >= 0 )
            combo->setCurrentIndex( idx );
          else
          {
            const int t = combo->findText( QString::number( d ) );
            if ( t >= 0 )
              combo->setCurrentIndex( t );
          }
        }
      }
      if ( !tip.isEmpty() )
        combo->setToolTip( tip );
      field.combo = combo;
      field.widget = combo;
      break;
    }
    case FieldKind::Double:
    {
      auto *spin = new QDoubleSpinBox( this );
      spin->setDecimals( 6 );
      double minV = -1.0e12;
      double maxV = 1.0e12;
      if ( prop.isMember( "minimum" ) && prop["minimum"].isNumeric() )
        minV = prop["minimum"].asDouble();
      if ( prop.isMember( "maximum" ) && prop["maximum"].isNumeric() )
        maxV = prop["maximum"].asDouble();
      spin->setRange( minV, maxV );
      if ( prop.isMember( "default" ) && prop["default"].isNumeric() )
        spin->setValue( prop["default"].asDouble() );
      if ( !tip.isEmpty() )
        spin->setToolTip( tip );
      field.doubleSpin = spin;
      field.widget = spin;
      break;
    }
    case FieldKind::Integer:
    {
      auto *spin = new QSpinBox( this );
      int minV = std::numeric_limits<int>::min() / 2;
      int maxV = std::numeric_limits<int>::max() / 2;
      if ( prop.isMember( "minimum" ) && prop["minimum"].isNumeric() )
        minV = prop["minimum"].asInt();
      if ( prop.isMember( "maximum" ) && prop["maximum"].isNumeric() )
        maxV = prop["maximum"].asInt();
      spin->setRange( minV, maxV );
      if ( prop.isMember( "default" ) && prop["default"].isNumeric() )
        spin->setValue( prop["default"].asInt() );
      if ( !tip.isEmpty() )
        spin->setToolTip( tip );
      field.spin = spin;
      field.widget = spin;
      break;
    }
    case FieldKind::Boolean:
    {
      auto *check = new QCheckBox( this );
      if ( prop.isMember( "default" ) )
        check->setChecked( isTruthyJson( prop["default"] ) );
      if ( !tip.isEmpty() )
        check->setToolTip( tip );
      field.check = check;
      field.widget = check;
      break;
    }
    case FieldKind::Array:
    {
      auto *edit = new QLineEdit( this );
      edit->setPlaceholderText( tr( "多个值用逗号/分号/换行分隔" ) );
      QString arrayTip = tip;
      if ( !arrayTip.isEmpty() )
        arrayTip += QStringLiteral( "\n" );
      arrayTip += tr( "数组参数：多个值用逗号、分号或换行分隔" );
      edit->setToolTip( arrayTip );
      if ( prop.isMember( "default" ) && prop["default"].isArray() )
      {
        QStringList parts;
        for ( const Json::Value &v : prop["default"] )
        {
          if ( v.isString() )
            parts << QString::fromStdString( v.asString() );
          else if ( v.isNumeric() )
            parts << QString::number( v.asDouble(), 'g', 16 );
        }
        edit->setText( parts.join( QStringLiteral( ", " ) ) );
      }
      else if ( prop.isMember( "default" ) && prop["default"].isString() )
      {
        edit->setText( QString::fromStdString( prop["default"].asString() ) );
      }
      field.lineEdit = edit;
      field.widget = edit;
      break;
    }
    case FieldKind::Object:
    {
      // 4.0: nested object → recursive sub-group. The label (incl. unit)
      // becomes the group title; children keep schema order.
      const QString label = fieldLabel( field.name, prop )
                            + ( field.unit.isEmpty()
                                    ? QString()
                                    : QStringLiteral( " (%1)" ).arg( field.unit ) );
      auto *box = new QGroupBox( label, this );
      box->setObjectName( QStringLiteral( "rsSchemaNestedObject" ) );
      if ( !tip.isEmpty() )
        box->setToolTip( tip );
      auto *form = new QFormLayout( box );
      form->setContentsMargins( 8, 12, 8, 8 );
      form->setHorizontalSpacing( 12 );
      form->setVerticalSpacing( 8 );

      field.children = buildChildFields( path, prop, depth, box, form );
      field.widget = box;
      break;
    }
    case FieldKind::ObjectArray:
    {
      // 4.0: repeatable object item editors. Rows are added by
      // appendArrayItem() (rebuild seeds minItems rows); the add button and
      // bounds hint live inside the host so the whole editor is one widget.
      // itemRows holds ONLY item rows (index == position); hint/button sit
      // beside it in the host layout.
      auto *host = new QWidget( this );
      auto *v = new QVBoxLayout( host );
      v->setContentsMargins( 0, 0, 0, 0 );
      v->setSpacing( 4 );

      auto *itemRows = new QVBoxLayout;
      itemRows->setContentsMargins( 0, 0, 0, 0 );
      itemRows->setSpacing( 4 );
      v->addLayout( itemRows );

      auto *hint = new QLabel( host );
      hint->setObjectName( QStringLiteral( "rsSchemaArrayHint" ) );
      hint->setWordWrap( true );
      hint->hide();
      field.arrayHint = hint;
      v->addWidget( hint );

      auto *add = new QPushButton( tr( "添加一项" ), host );
      add->setObjectName( QStringLiteral( "rsSchemaArrayAdd" ) );
      add->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
      v->addWidget( add, 0, Qt::AlignLeft );

      field.arrayHost = host;
      field.arrayLayout = itemRows;

      // Buttons navigate by path at click time: m_fields (and nested
      // children vectors) reallocate during builds, so a captured Field&
      // would dangle.
      const QString fieldPath = path;
      connect( add, &QPushButton::clicked, this, [this, fieldPath]()
      {
        if ( Field *f = findFieldMutable( m_fields, fieldPath ) )
          appendArrayItem( *f, true );
      } );

      field.widget = host;
      break;
    }
    case FieldKind::String:
    default:
    {
      auto *edit = new QLineEdit( this );
      if ( prop.isMember( "default" ) && prop["default"].isString() )
        edit->setText( QString::fromStdString( prop["default"].asString() ) );
      if ( !tip.isEmpty() )
        edit->setToolTip( tip );
      field.lineEdit = edit;
      field.widget = edit;
      break;
    }
  }

  // Default for string-like defaults on raster combo
  if ( field.kind == FieldKind::RasterCombo && field.combo
       && prop.isMember( "default" ) && prop["default"].isString() )
  {
    field.combo->setEditText( QString::fromStdString( prop["default"].asString() ) );
  }

  connectValueSignals( field );
  return field;
}

QVector<SchemaFormBuilder::Field>
SchemaFormBuilder::buildChildFields( const QString &basePath,
                                     const Json::Value &objectProp,
                                     int depth, QWidget *box, QFormLayout *form )
{
  QVector<Field> children;
  const auto props = orderedProperties( objectProp );
  const QStringList required = requiredListOf( objectProp );
  Q_UNUSED( required ); // required is enforced at validate() time

  for ( const auto &entry : props )
  {
    const QString &key = entry.first;
    const Json::Value &prop = entry.second;
    const QString childPath = basePath + QLatin1Char( '.' ) + key;
    Field child = buildField( childPath, prop, depth + 1 );

    const QString label = fieldLabel( key, prop )
                          + ( child.unit.isEmpty()
                                  ? QString()
                                  : QStringLiteral( " (%1)" ).arg( child.unit ) );
    // Accessibility parity with top-level fields: schema label as the
    // accessible name, canonical tooltip as the description.
    child.widget->setAccessibleName( label );
    const QString childDesc = tooltipFor( child );
    if ( !child.recommended.isEmpty() )
      child.widget->setToolTip( childDesc );
    if ( !childDesc.isEmpty() )
      child.widget->setAccessibleDescription( childDesc );
    applyParameterHelp( child, label );

    if ( child.kind == FieldKind::Object || child.kind == FieldKind::ObjectArray )
    {
      QLabel *spanLabel = new QLabel( label, box );
      spanLabel->setObjectName( QStringLiteral( "rsTaskPanelFieldLabel" ) );
      spanLabel->setProperty( "rsLabelForPath", childPath );
      form->addRow( spanLabel );
      form->addRow( child.widget );
      child.rowLabel = spanLabel;
    }
    else if ( child.kind == FieldKind::Boolean && child.check )
    {
      child.check->setText( label );
      form->addRow( QString(), child.widget );
    }
    else
    {
      auto *lab = new QLabel( label, box );
      lab->setObjectName( QStringLiteral( "rsTaskPanelFieldLabel" ) );
      lab->setBuddy( child.widget );
      form->addRow( lab, child.widget );
      child.rowLabel = lab;
    }
    children.push_back( std::move( child ) );
  }
  return children;
}

QWidget *SchemaFormBuilder::buildArrayItemRow( Field &field, int index )
{
  auto *row = new QWidget( field.arrayHost );
  auto *hl = new QHBoxLayout( row );
  hl->setContentsMargins( 0, 0, 0, 0 );
  hl->setSpacing( 6 );

  auto *box = new QGroupBox( tr( "项 %1" ).arg( index + 1 ), row );
  auto *form = new QFormLayout( box );
  form->setContentsMargins( 8, 12, 8, 8 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );

  QVector<Field> itemFields =
    buildChildFields( QStringLiteral( "%1.%2" ).arg( field.path ).arg( index ),
                      field.prop["items"], field.depth + 1, box, form );

  auto *rm = new QPushButton( tr( "移除" ), row );
  rm->setObjectName( QStringLiteral( "rsSchemaArrayRemove" ) );
  rm->setToolTip( tr( "移除该数组项" ) );

  const QString fieldPath = field.path;
  QPointer<QWidget> rowGuard( row );
  connect( rm, &QPushButton::clicked, this, [this, fieldPath, rowGuard]()
  {
    if ( !rowGuard )
      return;
    Field *f = findFieldMutable( m_fields, fieldPath );
    if ( !f || !f->arrayLayout )
      return;
    const int idx = f->arrayLayout->indexOf( rowGuard.data() );
    if ( idx >= 0 )
      removeArrayItem( *f, idx );
  } );

  hl->addWidget( box, 1 );
  hl->addWidget( rm );

  // Child signals are already connected inside buildField — connecting
  // again would double-emit valuesChanged on every edit (review A13).
  field.arrayItems.push_back( std::move( itemFields ) );
  return row;
}

void SchemaFormBuilder::appendArrayItem( Field &field, bool emitChange )
{
  if ( !field.arrayLayout )
    return;
  const int maxItems = maxItemsOf( field.prop );
  if ( maxItems >= 0 && field.arrayItems.size() >= maxItems )
  {
    updateArrayBoundsUi( field );
    return;
  }
  QWidget *row = buildArrayItemRow( field, field.arrayItems.size() );
  field.arrayLayout->addWidget( row );
  updateArrayBoundsUi( field );
  if ( emitChange )
    emit valuesChanged();
}

void SchemaFormBuilder::removeArrayItem( Field &field, int index )
{
  if ( !field.arrayLayout || index < 0 || index >= field.arrayItems.size() )
    return;
  if ( QLayoutItem *item = field.arrayLayout->takeAt( index ) )
  {
    if ( QWidget *w = item->widget() )
      w->deleteLater();
    delete item;
  }
  field.arrayItems.remove( index );
  // Renumber the visible titles so they keep matching the row positions.
  for ( int i = 0; i < field.arrayItems.size(); ++i )
  {
    if ( QLayoutItem *li = field.arrayLayout->itemAt( i ) )
    {
      if ( QWidget *row = li->widget() )
      {
        if ( QGroupBox *box = row->findChild<QGroupBox *>() )
          box->setTitle( tr( "项 %1" ).arg( i + 1 ) );
      }
    }
  }
  updateArrayBoundsUi( field );
  emit valuesChanged();
}

void SchemaFormBuilder::clearArrayItems( Field &field )
{
  if ( !field.arrayLayout )
    return;
  while ( QLayoutItem *item = field.arrayLayout->takeAt( 0 ) )
  {
    if ( QWidget *w = item->widget() )
      w->deleteLater();
    delete item;
  }
  field.arrayItems.clear();
}

void SchemaFormBuilder::updateArrayBoundsUi( Field &field )
{
  if ( !field.arrayHost )
    return;
  const int maxItems = maxItemsOf( field.prop );
  const int minItems = minItemsOf( field.prop );

  if ( auto *add = field.arrayHost->findChild<QPushButton *>(
         QStringLiteral( "rsSchemaArrayAdd" ) ) )
  {
    add->setEnabled( maxItems < 0
                     || field.arrayItems.size() < maxItems );
    add->setToolTip( maxItems >= 0
                       ? tr( "最多 %1 项" ).arg( maxItems )
                       : QString() );
  }
  for ( int i = 0; i < field.arrayItems.size(); ++i )
  {
    if ( QLayoutItem *li = field.arrayLayout->itemAt( i ) )
    {
      if ( QWidget *row = li->widget() )
      {
        if ( auto *rm = row->findChild<QPushButton *>(
               QStringLiteral( "rsSchemaArrayRemove" ) ) )
          rm->setEnabled( field.arrayItems.size() > minItems );
      }
    }
  }
  if ( field.arrayHint )
  {
    // Honest truncation: when setValues imported more than the hard cap,
    // the hint says exactly what was dropped.
    const QVariant truncated = field.arrayHost->property( "rsArrayTruncated" );
    if ( truncated.isValid() && truncated.toInt() > 0 )
    {
      field.arrayHint->setText( tr( "⚠ 数据包含 %1 项，仅加载前 %2 项（超出编辑上限）" )
                                  .arg( truncated.toInt() )
                                  .arg( field.arrayItems.size() ) );
      field.arrayHint->show();
    }
    else
    {
      field.arrayHint->hide();
    }
  }
}

void SchemaFormBuilder::connectValueSignals( Field &field )
{
  if ( field.combo )
  {
    connect( field.combo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, &SchemaFormBuilder::valuesChanged );
    if ( field.combo->isEditable() && field.combo->lineEdit() )
    {
      connect( field.combo->lineEdit(), &QLineEdit::textEdited,
               this, &SchemaFormBuilder::valuesChanged );
    }
  }
  if ( field.lineEdit )
  {
    connect( field.lineEdit, &QLineEdit::textEdited,
             this, &SchemaFormBuilder::valuesChanged );
  }
  if ( field.doubleSpin )
  {
    const QString path = field.path;
    connect( field.doubleSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, [this, path]( double ) {
               if ( Field *found = findFieldMutable( m_fields, path ) )
                 found->userTouched = true;
               emit valuesChanged();
             } );
  }
  if ( field.spin )
  {
    const QString path = field.path;
    connect( field.spin, QOverload<int>::of( &QSpinBox::valueChanged ),
             this, [this, path]( int ) {
               if ( Field *found = findFieldMutable( m_fields, path ) )
                 found->userTouched = true;
               emit valuesChanged();
             } );
  }
  if ( field.check )
  {
    connect( field.check, &QCheckBox::toggled,
             this, &SchemaFormBuilder::valuesChanged );
  }
  if ( field.plainEdit )
  {
    connect( field.plainEdit, &QPlainTextEdit::textChanged,
             this, &SchemaFormBuilder::valuesChanged );
  }
  if ( field.crsSelector )
  {
    connect( field.crsSelector, &CrsSelector::crsChanged,
             this, &SchemaFormBuilder::valuesChanged );
  }
}

void SchemaFormBuilder::rebuild( const Json::Value &schema )
{
  clearFields();
  m_schema = schema;

  const Json::Value *props = propertiesObject( schema );
  if ( !props || !props->isObject() )
    return;

  struct Entry
  {
    QString name;
    Json::Value prop;
    int order = 1000;
  };
  std::vector<Entry> entries;
  entries.reserve( props->getMemberNames().size() );

  for ( const std::string &key : props->getMemberNames() )
  {
    const Json::Value &prop = ( *props )[key];
    if ( !prop.isObject() )
      continue;
    Entry e;
    e.name = QString::fromStdString( key );
    // Prefer embedded "name" if present and non-empty.
    const QString embedded = memberString( prop, "name" );
    if ( !embedded.isEmpty() )
      e.name = embedded;
    e.prop = prop;
    e.order = uiOrder( prop );
    entries.push_back( std::move( e ) );
  }

  std::stable_sort( entries.begin(), entries.end(),
                    []( const Entry &a, const Entry &b )
  {
    if ( a.order != b.order )
      return a.order < b.order;
    return a.name < b.name;
  } );

  QGroupBox *inputBox = nullptr;
  QGroupBox *outputBox = nullptr;
  QGroupBox *paramsBox = nullptr;
  QGroupBox *advancedBox = nullptr;

  auto ensureBox = [&]( FieldGroup g ) -> QGroupBox *
  {
    switch ( g )
    {
      case FieldGroup::Input:
        if ( !inputBox )
          inputBox = makeSectionBox( this, tr( "输入" ), false );
        return inputBox;
      case FieldGroup::Output:
        if ( !outputBox )
          outputBox = makeSectionBox( this, tr( "输出" ), false );
        return outputBox;
      case FieldGroup::Params:
        if ( !paramsBox )
          paramsBox = makeSectionBox( this, tr( "参数" ), false );
        return paramsBox;
      case FieldGroup::Advanced:
        if ( !advancedBox )
          advancedBox = makeSectionBox( this, tr( "高级" ), true );
        return advancedBox;
    }
    return paramsBox;
  };

  for ( const Entry &e : entries )
  {
    Field field = buildField( e.name, e.prop, 0 );
    QGroupBox *box = ensureBox( field.group );
    auto *form = qobject_cast<QFormLayout *>( box->layout() );
    if ( !form || !field.widget )
      continue;

    const QString label = fieldLabel( e.name, e.prop )
                          + ( field.unit.isEmpty()
                                  ? QString()
                                  : QStringLiteral( " (%1)" ).arg( field.unit ) );
    // Accessible names for screen readers / keyboard navigation (Milestone G):
    // every labeled control carries its schema label.
    field.widget->setAccessibleName( label );
    const QString fieldDesc = tooltipFor( field );
    if ( !field.recommended.isEmpty() )
      field.widget->setToolTip( fieldDesc );
    if ( !fieldDesc.isEmpty() )
      field.widget->setAccessibleDescription( fieldDesc );

    // Unified Help 6.0: with an operator help context set (setHelpContext),
    // upgrade the tooltip with unit/recommended/trade-off knowledge and give
    // every field a What's This + helpId property so F1 lands on the
    // parameter topic.
    applyParameterHelp( field, label );

    QLabel *rowLabel = nullptr;
    if ( field.kind == FieldKind::Object || field.kind == FieldKind::ObjectArray )
    {
      // Nested objects carry their label as group title; both editors span
      // the full row width (a group box/column widget does not fit the
      // label column).
      rowLabel = makeSectionLabel( box, label );
      rowLabel->setProperty( "rsLabelForPath", field.path );
      form->addRow( rowLabel );
      form->addRow( field.widget );
    }
    else if ( field.kind == FieldKind::Boolean && field.check )
    {
      field.check->setText( label );
      form->addRow( QString(), field.widget );
    }
    else
    {
      rowLabel = new QLabel( label, box );
      rowLabel->setObjectName( QStringLiteral( "rsTaskPanelFieldLabel" ) );
      rowLabel->setBuddy( field.widget );
      form->addRow( rowLabel, field.widget );
    }
    field.rowLabel = rowLabel;
    m_fields.push_back( std::move( field ) );
  }
  updateConditionalVisibility();

  // Object arrays: seed minItems rows so required structures are visible
  // (and minItems validation can only fail on partially-filled rows).
  // Bounded by maxItems even when a malformed schema declares
  // minItems > maxItems (review A3 — the naive loop hangs the GUI thread).
  std::function<void( QVector<Field> & )> seedArrays = [&]( QVector<Field> &fields )
  {
    for ( Field &field : fields )
    {
      if ( field.kind != FieldKind::ObjectArray )
        continue;
      const int minItems = std::min( minItemsOf( field.prop ), kMaxObjectArrayItems );
      const int maxItems = maxItemsOf( field.prop );
      while ( field.arrayItems.size() < minItems
              && ( maxItems < 0 || field.arrayItems.size() < maxItems ) )
        appendArrayItem( field, false );
      updateArrayBoundsUi( field );
      seedArrays( field.children );
      for ( QVector<Field> &item : field.arrayItems )
        seedArrays( item );
    }
  };
  seedArrays( m_fields );

  // Section order: 输入 → 输出 → 参数 → 高级
  auto addSection = [this]( QGroupBox *box )
  {
    if ( !box )
      return;
    box->setObjectName( QStringLiteral( "rsTaskPanelSection" ) );
    // Checkable advanced keeps native title (acts as collapse toggle).
    // Other sections use a styled #rsTaskPanelSectionLabel header.
    if ( !box->isCheckable() )
    {
      if ( auto *form = qobject_cast<QFormLayout *>( box->layout() ) )
        form->insertRow( 0, makeSectionLabel( box, box->title() ) );
      box->setTitle( QString() );
    }
    m_root->addWidget( box );
  };

  addSection( inputBox );
  addSection( outputBox );
  addSection( paramsBox );
  addSection( advancedBox );

  // Inline validation summary (always present once a schema is built).
  m_validationLabel = new QLabel( this );
  m_validationLabel->setObjectName( QStringLiteral( "rsSchemaValidation" ) );
  m_validationLabel->setWordWrap( true );
  m_root->addWidget( m_validationLabel );

  m_root->addStretch( 1 );

  refreshChoices();
  updateValidationUi();
  scheduleAsyncChecks();
}

void SchemaFormBuilder::refreshRasterCombos()
{
  refreshComboChoices( FieldKind::RasterCombo, m_layerIds, m_layerNames );
}

void SchemaFormBuilder::refreshComboChoices( FieldKind kind,
                                             const QStringList &ids,
                                             const QStringList &names )
{
  refreshComboChoicesIn( m_fields, kind, ids, names );
}

void SchemaFormBuilder::refreshComboChoicesIn( QVector<Field> &fields,
                                               FieldKind kind,
                                               const QStringList &ids,
                                               const QStringList &names )
{
  // Recursive: nested-object and object-array children receive the same
  // choice sets as top-level fields (review A6 — a nested raster combo that
  // never populates is a dead editor).
  for ( Field &field : fields )
  {
    if ( field.kind == kind && field.combo )
    {
    const QString currentText = field.combo->currentText();
    QString currentData;
    if ( field.combo->currentIndex() >= 0 )
      currentData = field.combo->currentData().toString();

    field.combo->blockSignals( true );
    field.combo->clear();
    if ( kind == FieldKind::AssetCombo || kind == FieldKind::ModelCombo )
      field.combo->addItem( kind == FieldKind::AssetCombo ? tr( "选择数据资产…" )
                                                          : tr( "选择模型…" ),
                            QString() );
    const int n = std::min( ids.size(), names.size() );
    for ( int i = 0; i < n; ++i )
      field.combo->addItem( names.at( i ), ids.at( i ) );

    int idx = -1;
    if ( !currentData.isEmpty() )
      idx = field.combo->findData( currentData );
    if ( idx < 0 && !currentText.isEmpty() )
      idx = field.combo->findText( currentText );
    if ( idx >= 0 && idx != 0 )
    {
      field.combo->setCurrentIndex( idx );
    }
    else if ( kind == FieldKind::RasterCombo || kind == FieldKind::VectorCombo )
    {
      if ( !currentText.isEmpty() )
        field.combo->setEditText( currentText );
      else if ( !currentData.isEmpty() )
        field.combo->setEditText( currentData );
    }
    field.combo->blockSignals( false );
    }
    refreshComboChoicesIn( field.children, kind, ids, names );
    for ( QVector<Field> &item : field.arrayItems )
      refreshComboChoicesIn( item, kind, ids, names );
  }
}

void SchemaFormBuilder::refreshEnumSources()
{
  // Collect PATHS first, resolve each field fresh at apply time — raw Field*
  // would dangle if a provider re-entered the form (review B-8).
  QStringList enumPaths;
  std::function<void( const QVector<Field> & )> collect =
    [&]( const QVector<Field> &fields )
  {
    for ( const Field &f : fields )
    {
      // Workbench 9.0 M6: provider-resolved combos cover the dedicated
      // model/asset editor kinds too — a model/asset port annotated with
      // x-ui-enum-source resolves live (ModelCatalog / DataManager) instead
      // of rendering as an empty push-list combo. Kinds without an
      // enum-source keep the push channel unchanged.
      const bool providerKind = f.kind == FieldKind::Enum
                                || f.kind == FieldKind::ModelCombo
                                || f.kind == FieldKind::AssetCombo;
      if ( providerKind && !f.enumSource.isEmpty() && f.combo )
        enumPaths.append( f.path );
      collect( f.children );
      for ( int i = 0; i < f.arrayItems.size(); ++i )
        collect( f.arrayItems[i] );
    }
  };
  collect( m_fields );

  const Json::Value currentValues = values();
  for ( const QString &path : enumPaths )
  {
    Field *fieldPtr = findFieldMutable( m_fields, path );
    if ( !fieldPtr )
      continue;
    Field &field = *fieldPtr;
    QVector<SchemaEnumProvider::Choice> choices;
    const bool resolved =
      m_enumProvider
      && !( choices = m_enumProvider->choicesFor( field.enumSource, currentValues ) ).isEmpty();
    field.enumSourceResolved = resolved;

    const QString currentData = field.combo->currentData().toString();
    const QString currentText = field.combo->currentText();
    field.combo->blockSignals( true );
    field.combo->clear();
    if ( resolved )
    {
      field.combo->setEditable( false );
      for ( const SchemaEnumProvider::Choice &c : choices )
        field.combo->addItem( c.label.isEmpty() ? c.id : c.label, c.id );
      if ( !currentData.isEmpty() )
      {
        const int idx = field.combo->findData( currentData );
        if ( idx >= 0 )
          field.combo->setCurrentIndex( idx );
      }
      field.combo->setToolTip( tooltipFor( field ) );
    }
    else
    {
      // Honest degradation: provider missing or source unknown/empty →
      // free text with a visible hint, never a silent dead list.
      field.combo->setEditable( true );
      field.combo->setInsertPolicy( QComboBox::NoInsert );
      if ( !currentText.isEmpty() )
        field.combo->setEditText( currentText );
      const QString tip = tooltipFor( field )
                          + ( tooltipFor( field ).isEmpty() ? QString() : QStringLiteral( "\n" ) )
                          + tr( "⚠ 动态选项源“%1”暂不可用，可自由输入" ).arg( field.enumSource );
      field.combo->setToolTip( tip );
    }
    field.combo->blockSignals( false );
    field.widget->setProperty( "enumSourceResolved", resolved );
  }
}

void SchemaFormBuilder::refreshChoices()
{
  refreshComboChoices( FieldKind::RasterCombo, m_layerIds, m_layerNames );
  refreshComboChoices( FieldKind::VectorCombo, m_vectorIds, m_vectorNames );
  refreshComboChoices( FieldKind::AssetCombo, m_assetIds, m_assetNames );
  refreshComboChoices( FieldKind::ModelCombo, m_modelNames, m_modelNames );
  refreshEnumSources();
}

void SchemaFormBuilder::setRasterLayerChoices( const QStringList &layerIds,
                                               const QStringList &layerNames )
{
  m_layerIds = layerIds;
  m_layerNames = layerNames;
  refreshComboChoices( FieldKind::RasterCombo, layerIds, layerNames );
}

void SchemaFormBuilder::setVectorLayerChoices( const QStringList &layerIds,
                                               const QStringList &layerNames )
{
  m_vectorIds = layerIds;
  m_vectorNames = layerNames;
  refreshComboChoices( FieldKind::VectorCombo, layerIds, layerNames );
}

void SchemaFormBuilder::setAssetChoices( const QStringList &assetIds,
                                         const QStringList &assetNames )
{
  m_assetIds = assetIds;
  m_assetNames = assetNames;
  refreshComboChoices( FieldKind::AssetCombo, assetIds, assetNames );
}

void SchemaFormBuilder::setModelChoices( const QStringList &modelNames )
{
  m_modelNames = modelNames;
  refreshComboChoices( FieldKind::ModelCombo, modelNames, modelNames );
}

void SchemaFormBuilder::setEnumProvider( SchemaEnumProvider *provider )
{
  m_enumProvider = provider;
  refreshEnumSources();
}

void SchemaFormBuilder::setCheckPool( QThreadPool *pool )
{
  m_checkPool = pool;
}

void SchemaFormBuilder::runAsyncChecksNow()
{
  m_checkDebounce->stop();
  scheduleAsyncChecks();
}

QString SchemaFormBuilder::readFieldValue( const Field &field ) const
{
  switch ( field.kind )
  {
    case FieldKind::RasterCombo:
    case FieldKind::VectorCombo:
    case FieldKind::AssetCombo:
    case FieldKind::ModelCombo:
    case FieldKind::Enum:
      if ( field.combo )
      {
        if ( field.combo->isEditable() )
        {
          const QString text = field.combo->currentText();
          const int idx = field.combo->currentIndex();
          if ( idx >= 0 && field.combo->itemText( idx ) == text )
          {
            const QVariant data = field.combo->itemData( idx );
            if ( data.isValid() && !data.toString().isEmpty() )
              return data.toString();
          }
          return text;
        }
        const QVariant data = field.combo->currentData();
        // Non-editable combos (asset/model) carry a placeholder row with
        // empty data — that must read as "no value", never as its label.
        if ( data.isValid() && !data.toString().isEmpty() )
          return data.toString();
        if ( field.kind == FieldKind::AssetCombo || field.kind == FieldKind::ModelCombo )
          return QString();
        return field.combo->currentText();
      }
      break;
    case FieldKind::Array:
    case FieldKind::OutputPath:
    case FieldKind::String:
    case FieldKind::Color:
      if ( field.lineEdit )
        return field.lineEdit->text();
      break;
    case FieldKind::Crs:
      if ( field.crsSelector )
        return field.crsSelector->crsString();
      break;
    case FieldKind::Double:
      if ( field.doubleSpin )
        return QString::number( field.doubleSpin->value(), 'g', 16 );
      break;
    case FieldKind::Integer:
      if ( field.spin )
        return QString::number( field.spin->value() );
      break;
    case FieldKind::Boolean:
      if ( field.check )
        return field.check->isChecked() ? QStringLiteral( "true" )
                                        : QStringLiteral( "false" );
      break;
    case FieldKind::Object:
    case FieldKind::ObjectArray:
    case FieldKind::Json:
      break;
  }
  return {};
}

void SchemaFormBuilder::writeFieldValue( Field &field, const Json::Value &value )
{
  switch ( field.kind )
  {
    case FieldKind::RasterCombo:
    case FieldKind::VectorCombo:
      if ( field.combo )
      {
        QString s;
        if ( value.isString() )
          s = QString::fromStdString( value.asString() );
        else if ( value.isNumeric() )
          s = QString::number( value.asDouble(), 'g', 16 );
        if ( s.isEmpty() )
          break;
        int idx = field.combo->findData( s );
        if ( idx < 0 )
          idx = field.combo->findText( s );
        if ( idx >= 0 )
          field.combo->setCurrentIndex( idx );
        else if ( field.combo->isEditable() )
          field.combo->setEditText( s );
      }
      break;
    case FieldKind::AssetCombo:
    case FieldKind::ModelCombo:
    case FieldKind::Enum:
      if ( field.combo )
      {
        if ( value.isString() )
        {
          const QString s = QString::fromStdString( value.asString() );
          int idx = field.combo->findData( s );
          if ( idx < 0 )
            idx = field.combo->findText( s );
          if ( idx >= 0 )
            field.combo->setCurrentIndex( idx );
          else if ( field.combo->isEditable() && !s.isEmpty() )
            field.combo->setEditText( s );
        }
        else if ( value.isNumeric() )
        {
          const int idx = field.combo->findData( value.asDouble() );
          if ( idx >= 0 )
            field.combo->setCurrentIndex( idx );
        }
      }
      break;
    case FieldKind::Array:
      if ( field.lineEdit )
      {
        if ( value.isArray() )
        {
          QStringList parts;
          for ( const Json::Value &v : value )
          {
            if ( v.isString() )
              parts << QString::fromStdString( v.asString() );
            else if ( v.isNumeric() )
              parts << QString::number( v.asDouble(), 'g', 16 );
            else if ( v.isBool() )
              parts << ( v.asBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" ) );
          }
          field.lineEdit->setText( parts.join( QStringLiteral( ", " ) ) );
        }
        else if ( value.isString() )
        {
          field.lineEdit->setText( QString::fromStdString( value.asString() ) );
        }
        else if ( value.isNumeric() )
        {
          field.lineEdit->setText( QString::number( value.asDouble(), 'g', 16 ) );
        }
      }
      break;
    case FieldKind::OutputPath:
    case FieldKind::String:
    case FieldKind::Color:
      if ( field.lineEdit && value.isString() )
        field.lineEdit->setText( QString::fromStdString( value.asString() ) );
      else if ( field.lineEdit && value.isNumeric() )
        field.lineEdit->setText( QString::number( value.asDouble(), 'g', 16 ) );
      break;
    case FieldKind::Crs:
      if ( field.crsSelector && value.isString() )
        field.crsSelector->setCrsString( QString::fromStdString( value.asString() ) );
      break;
    case FieldKind::Json:
      if ( field.plainEdit && value.isString() )
        field.plainEdit->setPlainText( QString::fromStdString( value.asString() ) );
      else if ( field.plainEdit && ( value.isObject() || value.isArray() ) )
        field.plainEdit->setPlainText(
          QString::fromStdString( Json::StyledWriter().write( value ) ) );
      break;
    case FieldKind::Double:
      if ( field.doubleSpin && value.isNumeric() )
        field.doubleSpin->setValue( value.asDouble() );
      else if ( field.doubleSpin && value.isString() )
        field.doubleSpin->setValue( QString::fromStdString( value.asString() ).toDouble() );
      break;
    case FieldKind::Integer:
      if ( field.spin && value.isNumeric() )
        field.spin->setValue( value.asInt() );
      else if ( field.spin && value.isString() )
        field.spin->setValue( QString::fromStdString( value.asString() ).toInt() );
      break;
    case FieldKind::Boolean:
      if ( field.check )
        field.check->setChecked( isTruthyJson( value ) );
      break;
    case FieldKind::Object:
      // 4.0: nested round-trip.
      if ( value.isObject() )
        applyFields( field.children, value );
      break;
    case FieldKind::ObjectArray:
      // 4.0: rebuild item editors from the array (bounded, honest
      // truncation past the safety cap).
      if ( value.isArray() )
      {
        clearArrayItems( field );
        const int total = value.size();
        const int count = std::min( total, kMaxObjectArrayItems );
        if ( field.arrayHost )
          field.arrayHost->setProperty( "rsArrayTruncated",
                                        total > count ? total : 0 );
        for ( int i = 0; i < count; ++i )
        {
          QWidget *row = buildArrayItemRow( field, i );
          field.arrayLayout->addWidget( row );
          // setValues is signal-silent by contract (3.0): rows created here
          // did not exist when the outer block pass ran — block before any
          // value is applied; the outer unblock pass covers them after.
          setFieldsSignalsBlocked( field.arrayItems.last(), true );
          applyFields( field.arrayItems.last(), value[i] );
        }
        updateArrayBoundsUi( field );
      }
      break;
  }
}

void SchemaFormBuilder::collectFields( const QVector<Field> &fields,
                                       const QStringList &required,
                                       Json::Value &out ) const
{
  for ( const Field &field : fields )
  {
    // Milestone H: a field hidden by x-ui-visible-when is not collected (the
    // operator schema default applies until its condition holds).
    if ( field.condHidden )
      continue;
    // SchemaForm 4.0: an untouched optional group is ABSENT from values() —
    // the same rule validate() applies (an emitted empty/defaulted object
    // would be indistinguishable from a configured one).
    const bool isGroupRequired = required.contains( field.name );
    if ( field.kind == FieldKind::Object && !isGroupRequired
         && !groupTouched( field.children ) )
      continue;
    if ( field.kind == FieldKind::ObjectArray && !isGroupRequired
         && field.arrayItems.isEmpty() )
      continue;
    const std::string key = field.name.toStdString();
    switch ( field.kind )
    {
      case FieldKind::Double:
        if ( field.doubleSpin )
        {
          if ( !isGroupRequired && !field.userTouched )
            break;
          out[key] = field.doubleSpin->value();
        }
        break;
      case FieldKind::Integer:
        if ( field.spin )
        {
          if ( !isGroupRequired && !field.userTouched )
            break;
          out[key] = field.spin->value();
        }
        break;
      case FieldKind::Boolean:
        if ( field.check )
          out[key] = field.check->isChecked();
        break;
      case FieldKind::Enum:
        if ( field.combo )
        {
          const QVariant data = field.combo->currentData();
          if ( data.typeId() == QMetaType::Double || data.typeId() == QMetaType::Float
               || data.typeId() == QMetaType::Int || data.typeId() == QMetaType::LongLong )
            out[key] = data.toDouble();
          else
            out[key] = data.isValid() && !data.toString().isEmpty()
                         ? data.toString().toStdString()
                         : field.combo->currentText().toStdString();
        }
        break;
      case FieldKind::Array:
      {
        const QString raw = readFieldValue( field );
        // Determine item type: numeric if schema items.type is number/integer.
        bool numericItems = false;
        if ( field.prop.isObject() && field.prop.isMember( "items" ) && field.prop["items"].isObject() )
        {
          const QString itemType = memberString( field.prop["items"], "type" ).toLower();
          if ( itemType == QLatin1String( "number" ) || itemType == QLatin1String( "integer" ) )
            numericItems = true;
        }
        Json::Value arr( Json::arrayValue );
        const QStringList tokens = raw.split( QRegularExpression( QStringLiteral( "[,;\\n]+" ) ), Qt::SkipEmptyParts );
        for ( QString tok : tokens )
        {
          tok = tok.trimmed();
          if ( tok.isEmpty() )
            continue;
          if ( numericItems )
          {
            bool ok = false;
            double d = tok.toDouble( &ok );
            if ( ok )
            {
              // Preserve integer when possible.
              if ( tok.contains( QLatin1Char( '.' ) ) || tok.contains( QLatin1Char( 'e' ), Qt::CaseInsensitive ) )
                arr.append( d );
              else
              {
                bool intOk = false;
                long long iv = tok.toLongLong( &intOk );
                if ( intOk )
                  arr.append( Json::Value::Int64( iv ) );
                else
                  arr.append( d );
              }
              continue;
            }
          }
          arr.append( tok.toStdString() );
        }
        out[key] = arr;
        break;
      }
      case FieldKind::RasterCombo:
      case FieldKind::VectorCombo:
      case FieldKind::AssetCombo:
      case FieldKind::ModelCombo:
      case FieldKind::OutputPath:
      case FieldKind::String:
      case FieldKind::Color:
      case FieldKind::Crs:
        out[key] = readFieldValue( field ).toStdString();
        break;
      case FieldKind::Json:
        if ( field.plainEdit )
        {
          // Schema declares type:object — submit the parsed document, not
          // the raw text (validate() flags unparsable bodies).
          const std::string body = field.plainEdit->toPlainText().toStdString();
          Json::Value parsed;
          Json::CharReaderBuilder builder;
          std::string errors;
          std::istringstream stream( body );
          if ( !body.empty()
               && Json::parseFromStream( builder, stream, &parsed, &errors ) )
            out[key] = parsed;
          else
            out[key] = body;
        }
        break;
      case FieldKind::Object:
      {
        // 4.0: nested object → nested JSON (untouched optional groups never
        // reach here — filtered above by the same rule validate() uses).
        Json::Value nested( Json::objectValue );
        collectFields( field.children, requiredListOf( field.prop ), nested );
        out[key] = nested;
        break;
      }
      case FieldKind::ObjectArray:
      {
        // 4.0: one JSON object per item editor, in row order.
        Json::Value arr( Json::arrayValue );
        for ( const QVector<Field> &item : field.arrayItems )
        {
          Json::Value obj( Json::objectValue );
          collectFields( item, requiredListOf( field.prop["items"] ), obj );
          arr.append( obj );
        }
        out[key] = arr;
        break;
      }
    }
  }
}

Json::Value SchemaFormBuilder::values() const
{
  Json::Value out( Json::objectValue );
  collectFields( m_fields, requiredListOf( m_schema ), out );
  return out;
}

void SchemaFormBuilder::setFieldsSignalsBlocked( QVector<Field> &fields, bool blocked )
{
  for ( Field &field : fields )
  {
    if ( field.combo )
      field.combo->blockSignals( blocked );
    if ( field.lineEdit )
      field.lineEdit->blockSignals( blocked );
    if ( field.doubleSpin )
      field.doubleSpin->blockSignals( blocked );
    if ( field.spin )
      field.spin->blockSignals( blocked );
    if ( field.check )
      field.check->blockSignals( blocked );
    if ( field.plainEdit )
      field.plainEdit->blockSignals( blocked );
    if ( field.crsSelector )
      field.crsSelector->blockSignals( blocked );
    setFieldsSignalsBlocked( field.children, blocked );
    for ( QVector<Field> &item : field.arrayItems )
      setFieldsSignalsBlocked( item, blocked );
  }
}

void SchemaFormBuilder::applyFields( const QVector<Field> &fields, const Json::Value &params )
{
  if ( !params.isObject() )
    return;
  for ( const Field &field : fields )
  {
    const std::string key = field.name.toStdString();
    if ( !params.isMember( key ) )
      continue;
    Field &mutableField = const_cast<Field &>( field );
    writeFieldValue( mutableField, params[key] );
    mutableField.userTouched = true;
  }
}

void SchemaFormBuilder::setValues( const Json::Value &params )
{
  if ( !params.isObject() )
    return;

  setFieldsSignalsBlocked( m_fields, true );
  applyFields( m_fields, params );
  setFieldsSignalsBlocked( m_fields, false );

  // Review L #3: setValues suppresses every widget's signals, so the
  // valuesChanged → updateValidationUi path cannot fire. Re-evaluate the
  // x-ui-visible-when dependencies explicitly, or a field whose condition
  // just became true stays hidden and excluded from values()/validate().
  updateConditionalVisibility();
}

// ---------------------------------------------------------------------------
// Validation (Desktop Workbench UX 4.0, Milestone B)
// ---------------------------------------------------------------------------

void SchemaFormBuilder::validateFields(
  const QVector<Field> &fields, const QStringList &required,
  QList<ValidationIssue> &issues ) const
{
  for ( const Field &field : fields )
  {
    // Milestone H: conditionally hidden fields are not validated.
    if ( field.condHidden )
      continue;
    const bool isRequired = required.contains( field.name );
    const QString text = readFieldValue( field );

    switch ( field.kind )
    {
      case FieldKind::RasterCombo:
      case FieldKind::VectorCombo:
      case FieldKind::OutputPath:
      case FieldKind::String:
      case FieldKind::Crs:
        if ( isRequired && text.trimmed().isEmpty() )
          issues.append( { field.path,
                           tr( "必填参数“%1”不能为空" ).arg( field.name ), true } );
        break;
      case FieldKind::AssetCombo:
      case FieldKind::ModelCombo:
        if ( isRequired && ( !field.combo || field.combo->currentIndex() <= 0 ) )
          issues.append( { field.path,
                           tr( "必填参数“%1”未选择" ).arg( field.name ), true } );
        break;
      case FieldKind::Color:
        if ( !text.trimmed().isEmpty() && !QColor( text ).isValid() )
          issues.append( { field.path,
                           tr( "“%1”不是有效颜色（#RRGGBB）" ).arg( field.name ), true } );
        break;
      case FieldKind::Enum:
        // Non-editable dynamic-enum combos with unresolved sources cannot
        // distinguish "user picked nothing" from "nothing exists"; the
        // degraded editable form allows free text by contract. No extra
        // check beyond the required probe below.
        if ( isRequired && text.trimmed().isEmpty() )
          issues.append( { field.path,
                           tr( "必填参数“%1”未选择" ).arg( field.name ), true } );
        break;
      case FieldKind::Json:
      {
        if ( !field.plainEdit )
          break;
        const std::string body = field.plainEdit->toPlainText().toStdString();
        if ( body.empty() )
          break;
        Json::Value parsed;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream( body );
        if ( !Json::parseFromStream( builder, stream, &parsed, &errors ) )
          issues.append( { field.path,
                           tr( "“%1”不是有效 JSON：%2" )
                               .arg( field.name,
                                     QString::fromStdString( errors ).section( QLatin1Char( '\n' ), 0, 0 ) ),
                           true } );
        break;
      }
      case FieldKind::Array:
      {
        if ( isRequired && text.trimmed().isEmpty() )
          issues.append( { field.path,
                           tr( "必填参数“%1”不能为空" ).arg( field.name ), true } );
        if ( field.prop.isObject() && field.prop.isMember( "minItems" )
             && field.prop["minItems"].isNumeric() )
        {
          const QStringList tokens = text.split(
            QRegularExpression( QStringLiteral( "[,;\\n]+" ) ), Qt::SkipEmptyParts );
          const int minItems = field.prop["minItems"].asInt();
          if ( tokens.size() < minItems )
            issues.append( { field.path,
                             tr( "“%1”至少需要 %2 个值" ).arg( field.name ).arg( minItems ),
                             true } );
        }
        break;
      }
      case FieldKind::Object:
      {
        // 4.0: a nested object is validated against its own "required"
        // list. An OPTIONAL group whose fields are all empty counts as
        // absent (optional group); a required group or a touched optional
        // group validates its children.
        if ( !isRequired && !groupTouched( field.children ) )
          break;
        validateFields( field.children, requiredListOf( field.prop ), issues );
        break;
      }
      case FieldKind::ObjectArray:
      {
        // 4.0: item-count bounds then per-item validation against the
        // items schema's own "required" list.
        const int minItems = minItemsOf( field.prop );
        const int maxItems = maxItemsOf( field.prop );
        if ( field.arrayItems.size() < minItems )
          issues.append( { field.path,
                           tr( "“%1”至少需要 %2 项" ).arg( field.name ).arg( minItems ),
                           true } );
        if ( maxItems >= 0 && field.arrayItems.size() > maxItems )
          issues.append( { field.path,
                           tr( "“%1”最多允许 %2 项" ).arg( field.name ).arg( maxItems ),
                           true } );
        const QStringList itemRequired = requiredListOf( field.prop["items"] );
        for ( int i = 0; i < field.arrayItems.size(); ++i )
        {
          const QVector<Field> item = field.arrayItems.at( i );
          const int before = issues.size();
          validateFields( item, itemRequired, issues );
          // Rebase item issue paths onto the positional form
          // ("points.0.lat"): stored per-item paths may carry stale indices
          // after row removals, and the array path itself may contain dots.
          for ( int k = before; k < issues.size(); ++k )
          {
            QString tail = issues[k].fieldName;
            tail = tail.mid( field.path.size() + 1 );      // drop "<arrayPath>"
            const int dot = tail.indexOf( QLatin1Char( '.' ) );
            tail = dot >= 0 ? tail.mid( dot + 1 ) : QString(); // drop stale index
            issues[k].fieldName =
              QStringLiteral( "%1.%2" ).arg( field.path ).arg( i )
              + ( tail.isEmpty() ? QString() : QStringLiteral( "." ) + tail );
          }
        }
        break;
      }
      case FieldKind::Boolean:
        // A checkbox always carries a value (checked/unchecked are both
        // meaningful); nothing to validate.
        break;
      case FieldKind::Double:
      case FieldKind::Integer:
      {
        // Spin boxes clamp to the schema range; enum combos constrain values
        // by construction. Soft ranges (x-ui-soft-min/max) surface as
        // WARNING-level scientific-reasonability diagnostics.
        const double value = field.kind == FieldKind::Double
                                 ? ( field.doubleSpin ? field.doubleSpin->value() : 0.0 )
                                 : ( field.spin ? field.spin->value() : 0.0 );
        if ( field.prop.isObject() && field.prop.isMember( "x-ui-soft-min" )
             && field.prop["x-ui-soft-min"].isNumeric()
             && value < field.prop["x-ui-soft-min"].asDouble() )
          issues.append( { field.path,
                           tr( "“%1”低于建议下限 %2（科学合理性警告）" )
                               .arg( field.name )
                               .arg( field.prop["x-ui-soft-min"].asDouble() ),
                           false } );
        if ( field.prop.isObject() && field.prop.isMember( "x-ui-soft-max" )
             && field.prop["x-ui-soft-max"].isNumeric()
             && value > field.prop["x-ui-soft-max"].asDouble() )
          issues.append( { field.path,
                           tr( "“%1”高于建议上限 %2（科学合理性警告）" )
                               .arg( field.name )
                               .arg( field.prop["x-ui-soft-max"].asDouble() ),
                           false } );
        break;
      }
    }
  }
}

QList<SchemaFormBuilder::ValidationIssue> SchemaFormBuilder::validate() const
{
  QList<ValidationIssue> issues;

  // The schema root's "required" list is the source of truth.
  validateFields( m_fields, requiredListOf( m_schema ), issues );
  return issues;
}

bool SchemaFormBuilder::hasErrors() const
{
  for ( const ValidationIssue &issue : validate() )
  {
    if ( issue.isError )
      return true;
  }
  return false;
}

void SchemaFormBuilder::applyMarksFields(
  const QVector<Field> &fields, const QList<ValidationIssue> &issues ) const
{
  for ( const Field &field : fields )
  {
    if ( !field.widget )
      continue;
    bool hasError = false;
    for ( const ValidationIssue &issue : issues )
    {
      if ( issue.isError && issue.fieldName == field.path )
      {
        hasError = true;
        break;
      }
    }
    const bool hadError = field.widget->property( "errorState" ).toBool();
    field.widget->setProperty( "errorState", hasError );
    if ( hadError != hasError )
    {
      field.widget->style()->unpolish( field.widget );
      field.widget->style()->polish( field.widget );
    }
    if ( hasError )
    {
      for ( const ValidationIssue &issue : issues )
      {
        if ( issue.isError && issue.fieldName == field.path )
        {
          field.widget->setToolTip( issue.message );
          break;
        }
      }
    }
    else if ( hadError )
    {
      // Clearing an error restores the schema description + recommended
      // tooltip (review L #5: the recommended hint used to be lost after one
      // error→fix cycle until the next rebuild).
      field.widget->setToolTip( tooltipFor( field ) );
    }
    applyMarksFields( field.children, issues );
    for ( const QVector<Field> &item : field.arrayItems )
      applyMarksFields( item, issues );
  }
}

void SchemaFormBuilder::applyValidationMarks( const QList<ValidationIssue> &issues )
{
  applyMarksFields( m_fields, issues );

  if ( m_validationLabel )
  {
    int errors = 0;
    int warnings = 0;
    QString firstError;
    QString firstWarning;
    for ( const ValidationIssue &issue : issues )
    {
      if ( issue.isError )
      {
        ++errors;
        if ( firstError.isEmpty() )
          firstError = issue.message;
      }
      else
      {
        ++warnings;
        if ( firstWarning.isEmpty() )
          firstWarning = issue.message;
      }
    }
    if ( errors > 0 )
    {
      m_validationLabel->setProperty( "state", QStringLiteral( "error" ) );
      m_validationLabel->setText( tr( "⚠ %1 处参数无效：%2" ).arg( errors ).arg( firstError ) );
    }
    else if ( warnings > 0 )
    {
      // Review L #4: the warning CONTENT must be reachable — the label names
      // the first warning verbatim (not just a count), so keyboard-only and
      // screen-reader users can act on it.
      m_validationLabel->setProperty( "state", QStringLiteral( "warn" ) );
      m_validationLabel->setText( tr( "△ %1 条提示：%2" ).arg( warnings ).arg( firstWarning ) );
    }
    else
    {
      m_validationLabel->setProperty( "state", QStringLiteral( "ok" ) );
      m_validationLabel->setText( tr( "✓ 参数有效" ) );
    }
    m_validationLabel->style()->unpolish( m_validationLabel );
    m_validationLabel->style()->polish( m_validationLabel );
  }
}

void SchemaFormBuilder::clearValidationMarks()
{
  applyValidationMarks( {} );
}

void SchemaFormBuilder::updateValidationUi()
{
  // Milestone H: dependencies first — a field hidden by its x-ui-visible-when
  // condition must not be validated (or collected) while hidden.
  updateConditionalVisibility();
  const QList<ValidationIssue> issues = validate();
  applyValidationMarks( issues );
  bool blocking = false;
  for ( const ValidationIssue &issue : issues )
  {
    if ( issue.isError )
    {
      blocking = true;
      break;
    }
  }
  emit validationChanged( blocking );
}

QString SchemaFormBuilder::leafValueText( const QVector<Field> &fields,
                                          const QString &name ) const
{
  for ( const Field &f : fields )
  {
    if ( f.name == name && f.widget )
      return readFieldValue( f );
  }
  for ( const Field &f : fields )
  {
    if ( !f.children.isEmpty() )
    {
      const QString v = leafValueText( f.children, name );
      if ( !v.isEmpty() )
        return v;
    }
    for ( const QVector<Field> &item : f.arrayItems )
    {
      const QString v = leafValueText( item, name );
      if ( !v.isEmpty() )
        return v;
    }
  }
  return {};
}

void SchemaFormBuilder::updateConditionalVisibility( QVector<Field> &fields )
{
  // Numeric-tolerant scalar comparison: "1", "1.0" and "true"→1 style values
  // must match regardless of how the editor formats its text.
  auto sameScalar = []( const QString &a, const QString &b ) {
    if ( a.compare( b, Qt::CaseInsensitive ) == 0 )
      return true;
    bool okA = false, okB = false;
    const double da = a.toDouble( &okA );
    const double db = b.toDouble( &okB );
    return okA && okB && qFuzzyCompare( da, db );
  };

  for ( Field &field : fields )
  {
    if ( !field.widget )
      continue;

    bool visible = true;
    if ( field.visibleWhen.isObject() && !field.visibleWhen.empty() )
    {
      for ( auto it = field.visibleWhen.begin(); it != field.visibleWhen.end(); ++it )
      {
        const QString param = QString::fromUtf8( it.memberName() );
        QString expected;
        if ( it->isString() )
          expected = QString::fromStdString( it->asString() );
        else if ( it->isBool() )
          expected = it->asBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" );
        else if ( it->isNumeric() )
          expected = QString::number( it->asDouble() );
        if ( !sameScalar( leafValueText( m_fields, param ), expected ) )
        {
          visible = false;
          break;
        }
      }
    }

    const bool becomingVisible = field.condHidden && visible;
    field.condHidden = !visible;
    // Revealing a conditional numeric by changing its driver counts as a
    // user choice to include the schema default (x-ui-visible-when tests).
    if ( becomingVisible )
      field.userTouched = true;
    if ( field.widget->isVisibleTo( field.widget->parentWidget() ) != visible )
      field.widget->setVisible( visible );
    // Row label follows the widget: for label+buddy rows the form knows the
    // pairing; for spanning rows (nested objects / arrays) the builder
    // recorded the label on the field.
    QLabel *rowLabel = field.rowLabel;
    if ( !rowLabel && field.widget->parentWidget() )
    {
      if ( auto *form = qobject_cast<QFormLayout *>( field.widget->parentWidget()->layout() ) )
        rowLabel = qobject_cast<QLabel *>( form->labelForField( field.widget ) );
    }
    if ( rowLabel )
      rowLabel->setVisible( visible );
    updateConditionalVisibility( field.children );
    for ( QVector<Field> &item : field.arrayItems )
      updateConditionalVisibility( item );
  }
}

void SchemaFormBuilder::updateConditionalVisibility()
{
  if ( m_fields.isEmpty() )
    return;
  updateConditionalVisibility( m_fields );
}

QString SchemaFormBuilder::tooltipFor( const Field &field ) const
{
  // Canonical tooltip: schema description first, then the recommended-value
  // hint. Used by rebuild AND by applyValidationMarks when an error clears,
  // so the recommended hint survives error→fix cycles (review L #5).
  QString tip = memberString( field.prop, "description" );
  if ( !field.recommended.isEmpty() )
  {
    const QString recommendedText = tr( "推荐值：%1" ).arg( field.recommended );
    tip += tip.isEmpty() ? recommendedText : QStringLiteral( " " ) + recommendedText;
  }
  return tip;
}

bool SchemaFormBuilder::groupTouched( const QVector<Field> &fields ) const
{
  for ( const Field &child : fields )
  {
    if ( child.condHidden )
      continue;
    if ( child.kind == FieldKind::Boolean )
    {
      // Unchecked counts as untouched (Qt convention: unchecked = default);
      // a checked box always means the group is in use.
      if ( child.check && child.check->isChecked() )
        return true;
    }
    else if ( child.kind == FieldKind::ObjectArray )
    {
      if ( !child.arrayItems.isEmpty() )
        return true;
    }
    else if ( child.kind == FieldKind::Json )
    {
      // readFieldValue is empty for the JSON editor — inspect its text.
      if ( child.plainEdit && !child.plainEdit->toPlainText().trimmed().isEmpty() )
        return true;
    }
    else if ( child.kind == FieldKind::Object )
    {
      if ( groupTouched( child.children ) )
        return true;
    }
    else if ( child.kind == FieldKind::Integer || child.kind == FieldKind::Double )
    {
      // Spinboxes always have a value (schema default or Qt 0). They do not
      // mark the parent group in-use until the user edits them or setValues
      // supplies the key — the same omit rule collectFields applies.
      if ( child.userTouched )
        return true;
    }
    else if ( !readFieldValue( child ).trimmed().isEmpty() )
    {
      return true;
    }
  }
  return false;
}

SchemaFormBuilder::Field *
SchemaFormBuilder::findFieldMutable( QVector<Field> &fields, const QString &path )
{
  for ( Field &f : fields )
  {
    if ( f.path == path )
      return &f;
    if ( Field *nested = findFieldMutable( f.children, path ) )
      return nested;
  }
  // Array item paths carry stale indices after row removals; match on the
  // array prefix so the lookup stays position-independent.
  for ( Field &f : fields )
  {
    if ( f.kind != FieldKind::ObjectArray )
      continue;
    const QString prefix = f.path + QLatin1Char( '.' );
    if ( !path.startsWith( prefix ) )
      continue;
    for ( QVector<Field> &item : f.arrayItems )
    {
      if ( Field *hit = findFieldMutable( item, path ) )
        return hit;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Async value checks (SchemaForm 4.0) — bounded pool + generation cancel
// ---------------------------------------------------------------------------

void SchemaFormBuilder::scheduleAsyncChecks()
{
  ++m_checkGeneration;
  const quint64 gen = m_checkGeneration;

  QVector<CheckJob> jobs;
  std::function<void( const QVector<Field> & )> collect =
    [&]( const QVector<Field> &fields )
  {
    for ( const Field &f : fields )
    {
      if ( f.condHidden )
      {
        // Hidden fields report nothing; their marks clear on the next apply.
      }
      else
      {
        for ( const QString &check : f.checks )
        {
          CheckJob job;
          job.path = f.path;
          job.check = check;
          job.value = readFieldValue( f ).trimmed();
          job.canonicalTip = tooltipFor( f );
          job.target = f.widget;
          jobs.push_back( job );
        }
      }
      collect( f.children );
      for ( const QVector<Field> &item : f.arrayItems )
        collect( item );
    }
  };
  collect( m_fields );
  if ( jobs.isEmpty() )
    return;

  QThreadPool &pool = m_checkPool ? *m_checkPool : sicnu::app::RsScanPool::instance().pool();
  for ( const CheckJob &job : jobs )
  {
    if ( job.value.isEmpty() )
    {
      // A cleared value cannot satisfy any check — reset the field's marks
      // to neutral instead of leaving a stale failure behind (review A10).
      if ( job.target )
      {
        job.target->setProperty(
          QStringLiteral( "check_%1" ).arg( job.check ).toUtf8().constData(),
          QVariant() );
        job.target->setToolTip( job.canonicalTip );
      }
      continue;
    }
    QPointer<SchemaFormBuilder> self( this );
    pool.start( [self, gen, job]()
    {
      // Worker side: no widget access — pure value computation.
      bool ok = true;
      if ( job.check == QLatin1String( "path_exists" ) )
        ok = QFileInfo::exists( job.value );
      if ( !self )
        return; // form died while queued: drop, never touch dead state
      const bool result = ok;
      QMetaObject::invokeMethod(
        self.data(),
        [self, gen, job, result]()
        {
          if ( self )
            self->applyAsyncCheckResult( gen, job.target, job.check, result,
                                         job.canonicalTip );
        },
        Qt::QueuedConnection );
    } );
  }
}

void SchemaFormBuilder::applyAsyncCheckResult( quint64 generation,
                                               const QPointer<QWidget> &target,
                                               const QString &check, bool ok,
                                               const QString &canonicalTip )
{
  if ( generation != m_checkGeneration )
    return; // superseded by a newer rebuild/edit — stale result dropped
  if ( target.isNull() )
    return; // the marked editor died — nothing to touch
  const QString key = QStringLiteral( "check_%1" ).arg( check );
  target->setProperty( key.toUtf8().constData(), ok );
  // The canonical tooltip stays authoritative; a failed check appends a
  // visible, screen-reader-reachable hint. Re-applied on EVERY delivery:
  // updateValidationUi's error-mark pass rewrites tooltips, so an unchanged
  // failed result must still restore its hint (review A8).
  QString tip = canonicalTip;
  if ( !ok )
  {
    if ( check == QLatin1String( "path_exists" ) )
      tip += ( tip.isEmpty() ? QString() : QStringLiteral( "\n" ) )
             + tr( "⚠ 路径不存在（%1）" ).arg( check );
  }
  target->setToolTip( tip );
}
