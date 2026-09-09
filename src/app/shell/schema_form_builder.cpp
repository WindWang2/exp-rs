/***************************************************************************
 * schema_form_builder.cpp  —  schema JSON → validated Qt form widgets
 ***************************************************************************/
#include "schema_form_builder.h"

#include "help/help_id.h"
#include "help/help_presenter.h"
#include "help/help_registry.h"
#include "widgets/crs_selector.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaType>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
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
SchemaFormBuilder::classifyKind( const QString &name, const Json::Value &prop )
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

void SchemaFormBuilder::setHelpContext( const QString &operatorId )
{
  m_helpOperatorId = operatorId;
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

SchemaFormBuilder::Field
SchemaFormBuilder::buildField( const QString &name, const Json::Value &prop )
{
  Field field;
  field.name = name;
  field.kind = classifyKind( name, prop );
  field.group = classifyGroup( name, prop );
  field.prop = prop;

  // Advanced may still reclassify group if x-ui-advanced set (already in classifyGroup).
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
    connect( field.doubleSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, &SchemaFormBuilder::valuesChanged );
  }
  if ( field.spin )
  {
    connect( field.spin, QOverload<int>::of( &QSpinBox::valueChanged ),
             this, &SchemaFormBuilder::valuesChanged );
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
    Field field = buildField( e.name, e.prop );
    QGroupBox *box = ensureBox( field.group );
    auto *form = qobject_cast<QFormLayout *>( box->layout() );
    if ( !form || !field.widget )
      continue;

    const QString label = fieldLabel( e.name, e.prop );
    // Accessible names for screen readers / keyboard navigation (Milestone G):
    // every labeled control carries its schema label.
    field.widget->setAccessibleName( label );
    const QString fieldDesc = memberString( e.prop, "description" );
    if ( !fieldDesc.isEmpty() )
      field.widget->setAccessibleDescription( fieldDesc );

    // Unified Help 6.0: with an operator help context set (setHelpContext),
    // upgrade the tooltip with unit/recommended/trade-off knowledge and give
    // every field a What's This + helpId property so F1 lands on the
    // parameter topic.
    applyParameterHelp( field, label );

    if ( field.kind == FieldKind::Boolean && field.check )
    {
      field.check->setText( label );
      form->addRow( QString(), field.widget );
    }
    else
    {
      auto *lab = new QLabel( label, box );
      lab->setObjectName( QStringLiteral( "rsTaskPanelFieldLabel" ) );
      lab->setBuddy( field.widget );
      form->addRow( lab, field.widget );
    }
    m_fields.push_back( field );
  }

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
}

void SchemaFormBuilder::refreshRasterCombos()
{
  refreshComboChoices( FieldKind::RasterCombo, m_layerIds, m_layerNames );
}

void SchemaFormBuilder::refreshComboChoices( FieldKind kind,
                                             const QStringList &ids,
                                             const QStringList &names )
{
  for ( Field &field : m_fields )
  {
    if ( field.kind != kind || !field.combo )
      continue;

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
}

void SchemaFormBuilder::refreshChoices()
{
  refreshComboChoices( FieldKind::RasterCombo, m_layerIds, m_layerNames );
  refreshComboChoices( FieldKind::VectorCombo, m_vectorIds, m_vectorNames );
  refreshComboChoices( FieldKind::AssetCombo, m_assetIds, m_assetNames );
  refreshComboChoices( FieldKind::ModelCombo, m_modelNames, m_modelNames );
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
  }
}

Json::Value SchemaFormBuilder::values() const
{
  Json::Value out( Json::objectValue );
  for ( const Field &field : m_fields )
  {
    const std::string key = field.name.toStdString();
    switch ( field.kind )
    {
      case FieldKind::Double:
        if ( field.doubleSpin )
          out[key] = field.doubleSpin->value();
        break;
      case FieldKind::Integer:
        if ( field.spin )
          out[key] = field.spin->value();
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
      default:
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
    }
  }
  return out;
}

void SchemaFormBuilder::setValues( const Json::Value &params )
{
  if ( !params.isObject() )
    return;

  for ( Field &field : m_fields )
  {
    const std::string key = field.name.toStdString();
    if ( !params.isMember( key ) )
      continue;

    if ( field.combo )
      field.combo->blockSignals( true );
    if ( field.lineEdit )
      field.lineEdit->blockSignals( true );
    if ( field.doubleSpin )
      field.doubleSpin->blockSignals( true );
    if ( field.spin )
      field.spin->blockSignals( true );
    if ( field.check )
      field.check->blockSignals( true );
    if ( field.plainEdit )
      field.plainEdit->blockSignals( true );
    if ( field.crsSelector )
      field.crsSelector->blockSignals( true );

    writeFieldValue( field, params[key] );

    if ( field.combo )
      field.combo->blockSignals( false );
    if ( field.lineEdit )
      field.lineEdit->blockSignals( false );
    if ( field.doubleSpin )
      field.doubleSpin->blockSignals( false );
    if ( field.spin )
      field.spin->blockSignals( false );
    if ( field.check )
      field.check->blockSignals( false );
    if ( field.plainEdit )
      field.plainEdit->blockSignals( false );
    if ( field.crsSelector )
      field.crsSelector->blockSignals( false );
  }
}

// ---------------------------------------------------------------------------
// Validation (Desktop Workbench UX 4.0, Milestone B)
// ---------------------------------------------------------------------------

QList<SchemaFormBuilder::ValidationIssue> SchemaFormBuilder::validate() const
{
  QList<ValidationIssue> issues;

  // The schema root's "required" list is the source of truth.
  QStringList required;
  if ( m_schema.isObject() && m_schema.isMember( "required" )
       && m_schema["required"].isArray() )
  {
    for ( const Json::Value &r : m_schema["required"] )
    {
      if ( r.isString() )
        required << QString::fromStdString( r.asString() );
    }
  }

  for ( const Field &field : m_fields )
  {
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
          issues.append( { field.name,
                           tr( "必填参数“%1”不能为空" ).arg( field.name ), true } );
        break;
      case FieldKind::AssetCombo:
      case FieldKind::ModelCombo:
        if ( isRequired && ( !field.combo || field.combo->currentIndex() <= 0 ) )
          issues.append( { field.name,
                           tr( "必填参数“%1”未选择" ).arg( field.name ), true } );
        break;
      case FieldKind::Color:
        if ( !text.trimmed().isEmpty() && !QColor( text ).isValid() )
          issues.append( { field.name,
                           tr( "“%1”不是有效颜色（#RRGGBB）" ).arg( field.name ), true } );
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
          issues.append( { field.name,
                           tr( "“%1”不是有效 JSON：%2" )
                               .arg( field.name,
                                     QString::fromStdString( errors ).section( QLatin1Char( '\n' ), 0, 0 ) ),
                           true } );
        break;
      }
      case FieldKind::Array:
      {
        if ( isRequired && text.trimmed().isEmpty() )
          issues.append( { field.name,
                           tr( "必填参数“%1”不能为空" ).arg( field.name ), true } );
        if ( field.prop.isObject() && field.prop.isMember( "minItems" )
             && field.prop["minItems"].isNumeric() )
        {
          const QStringList tokens = text.split(
            QRegularExpression( QStringLiteral( "[,;\\n]+" ) ), Qt::SkipEmptyParts );
          const int minItems = field.prop["minItems"].asInt();
          if ( tokens.size() < minItems )
            issues.append( { field.name,
                             tr( "“%1”至少需要 %2 个值" ).arg( field.name ).arg( minItems ),
                             true } );
        }
        break;
      }
      case FieldKind::Double:
      case FieldKind::Integer:
      case FieldKind::Enum:
      case FieldKind::Boolean:
        // Spin boxes clamp to the schema range; enum combos constrain values
        // by construction — no further inline check needed.
        break;
    }
  }

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

void SchemaFormBuilder::applyValidationMarks( const QList<ValidationIssue> &issues )
{
  for ( const Field &field : m_fields )
  {
    if ( !field.widget )
      continue;
    bool hasError = false;
    for ( const ValidationIssue &issue : issues )
    {
      if ( issue.isError && issue.fieldName == field.name )
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
        if ( issue.isError && issue.fieldName == field.name )
        {
          field.widget->setToolTip( issue.message );
          break;
        }
      }
    }
    else if ( hadError )
    {
      // Clearing an error restores the schema description tooltip.
      const QString desc = memberString( field.prop, "description" );
      field.widget->setToolTip( desc );
    }
  }

  if ( m_validationLabel )
  {
    int errors = 0;
    int warnings = 0;
    QString firstError;
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
      }
    }
    if ( errors > 0 )
    {
      m_validationLabel->setProperty( "state", QStringLiteral( "error" ) );
      m_validationLabel->setText( tr( "⚠ %1 处参数无效：%2" ).arg( errors ).arg( firstError ) );
    }
    else if ( warnings > 0 )
    {
      m_validationLabel->setProperty( "state", QStringLiteral( "warn" ) );
      m_validationLabel->setText( tr( "△ %1 条提示" ).arg( warnings ) );
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
