/***************************************************************************
 * schema_form_builder.cpp  —  schema JSON → Qt form widgets for task panel
 ***************************************************************************/
#include "schema_form_builder.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaType>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
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
}

void SchemaFormBuilder::clearFields()
{
  m_fields.clear();
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
}

void SchemaFormBuilder::rebuild( const Json::Value &schema )
{
  clearFields();

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
    if ( field.kind == FieldKind::Boolean && field.check )
    {
      field.check->setText( label );
      form->addRow( QString(), field.widget );
    }
    else
    {
      auto *lab = new QLabel( label, box );
      lab->setObjectName( QStringLiteral( "rsTaskPanelFieldLabel" ) );
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

  m_root->addStretch( 1 );

  refreshRasterCombos();
}

void SchemaFormBuilder::refreshRasterCombos()
{
  for ( Field &field : m_fields )
  {
    if ( field.kind != FieldKind::RasterCombo || !field.combo )
      continue;

    const QString currentText = field.combo->currentText();
    QString currentData;
    if ( field.combo->currentIndex() >= 0 )
      currentData = field.combo->currentData().toString();

    field.combo->blockSignals( true );
    field.combo->clear();
    const int n = std::min( m_layerIds.size(), m_layerNames.size() );
    for ( int i = 0; i < n; ++i )
      field.combo->addItem( m_layerNames.at( i ), m_layerIds.at( i ) );

    int idx = -1;
    if ( !currentData.isEmpty() )
      idx = field.combo->findData( currentData );
    if ( idx < 0 && !currentText.isEmpty() )
      idx = field.combo->findText( currentText );
    if ( idx >= 0 )
    {
      field.combo->setCurrentIndex( idx );
    }
    else if ( !currentText.isEmpty() )
    {
      field.combo->setEditText( currentText );
    }
    else if ( !currentData.isEmpty() )
    {
      field.combo->setEditText( currentData );
    }
    field.combo->blockSignals( false );
  }
}

void SchemaFormBuilder::setRasterLayerChoices( const QStringList &layerIds,
                                               const QStringList &layerNames )
{
  m_layerIds = layerIds;
  m_layerNames = layerNames;
  refreshRasterCombos();
}

QString SchemaFormBuilder::readFieldValue( const Field &field ) const
{
  switch ( field.kind )
  {
    case FieldKind::RasterCombo:
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
        if ( data.isValid() && !data.toString().isEmpty() )
          return data.toString();
        return field.combo->currentText();
      }
      break;
    case FieldKind::Array:
    case FieldKind::OutputPath:
    case FieldKind::String:
      if ( field.lineEdit )
        return field.lineEdit->text();
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
      if ( field.lineEdit && value.isString() )
        field.lineEdit->setText( QString::fromStdString( value.asString() ) );
      else if ( field.lineEdit && value.isNumeric() )
        field.lineEdit->setText( QString::number( value.asDouble(), 'g', 16 ) );
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
      case FieldKind::OutputPath:
      case FieldKind::String:
      default:
        out[key] = readFieldValue( field ).toStdString();
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
  }
}
