// src/agent/cartography/registry.cpp
#include "registry.h"

#include "../mapspec/mapspec.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMutexLocker>

#include <json/reader.h>

#include <vector>

namespace sicnu::agent::cartography {

namespace {

QString defaultComponentsDir()
{
  if ( qEnvironmentVariableIsSet( "SICNU_CARTOGRAPHY_DIR" ) )
  {
    const QString base = qEnvironmentVariable( "SICNU_CARTOGRAPHY_DIR" );
    return base;
  }
  return QDir::current().filePath( QStringLiteral( "data/cartography" ) );
}

bool parseJsonFile( const QString &path, Json::Value *out, QString *error )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    if ( error )
      *error = QStringLiteral( "cannot read %1" ).arg( path );
    return false;
  }
  const QByteArray bytes = file.readAll();
  Json::CharReaderBuilder builder;
  std::string parseErrors;
  if ( !Json::parseFromStream( builder, file, out, &parseErrors ) )
  {
    if ( error )
      *error = QStringLiteral( "%1: %2" ).arg( path, QString::fromStdString( parseErrors ) );
    return false;
  }
  Q_UNUSED( bytes );
  return true;
}

/// Descriptor-level validation shared by the loader and registerComponent.
std::vector<std::string> validateComponent( const Json::Value &descriptor,
                                            const QStringList &knownCategories )
{
  std::vector<std::string> problems;
  if ( !descriptor.isObject() )
    return { "component must be an object" };
  const std::string id = descriptor.isMember( "id" ) && descriptor["id"].isString()
                           ? descriptor["id"].asString()
                           : "";
  if ( id.empty() )
    problems.push_back( "component needs a string id" );
  if ( !descriptor.isMember( "category" ) || !descriptor["category"].isString() )
    problems.push_back( id + ": needs a string category" );
  else if ( !knownCategories.contains( QString::fromStdString( descriptor["category"].asString() ) ) )
    problems.push_back( id + ": unknown category '" + descriptor["category"].asString() + "'" );
  if ( descriptor.isMember( "parameters" ) && !descriptor["parameters"].isObject() )
    problems.push_back( id + ": parameters must be an object" );
  if ( descriptor.isMember( "layout_constraints" ) && !descriptor["layout_constraints"].isObject() )
    problems.push_back( id + ": layout_constraints must be an object" );
  return problems;
}

/// Minimal embedded safety set (mirrors data/cartography/components/*.json
/// essentials) so headless runs without the data dir still work.
Json::Value embeddedComponents()
{
  Json::Value list( Json::arrayValue );
  Json::Value arrow( Json::objectValue );
  arrow["id"] = "north-arrow/minimal";
  arrow["category"] = "north-arrow";
  arrow["semantic_roles"] = Json::Value( Json::arrayValue );
  arrow["semantic_roles"].append( "north_arrow.primary" );
  arrow["parameters"] = Json::Value( Json::objectValue );
  arrow["parameters"]["size_mm"] = 10;
  list.append( arrow );

  Json::Value scaleBar( Json::objectValue );
  scaleBar["id"] = "scale-bar/single";
  scaleBar["category"] = "scale-bar";
  scaleBar["parameters"] = Json::Value( Json::objectValue );
  scaleBar["parameters"]["style"] = "Single Box";
  scaleBar["parameters"]["units"] = "km";
  list.append( scaleBar );

  Json::Value legend( Json::objectValue );
  legend["id"] = "legend/categorical";
  legend["category"] = "legend";
  legend["parameters"] = Json::Value( Json::objectValue );
  legend["parameters"]["title"] = "图例";
  list.append( legend );
  return list;
}

Json::Value embeddedTemplates()
{
  // One minimal draft; the shipped data/cartography/templates/*.json set is
  // the real catalog when resolvable.
  Json::Value list( Json::arrayValue );
  Json::Value tmpl( Json::objectValue );
  tmpl["id"] = "remote-sensing-result";
  tmpl["description"] = "Generic continuous remote-sensing result.";
  Json::Value slots( Json::arrayValue );
  const auto slot = [ &slots = slots ]( const char *role, const char *collection, double x,
                                        double y, double w, double h ) {
    Json::Value s( Json::objectValue );
    s["role"] = role;
    s["accepts"] = collection;
    Json::Value rect( Json::arrayValue );
    rect.append( x );
    rect.append( y );
    rect.append( w );
    rect.append( h );
    s["rect_mm"] = rect;
    slots.append( s );
  };
  slot( "map.main", "map_frames", 12, 24, 190, 160 );
  slot( "title.main", "titles", 12, 6, 200, 14 );
  slot( "colorbar.primary", "colorbars", 60, 190, 90, 8 );
  slot( "scalebar.primary", "scale_bars", 14, 188, 60, 8 );
  slot( "source.primary", "source_notes", 170, 190, 110, 8 );
  tmpl["required_slots"] = slots;
  list.append( tmpl );
  return list;
}

} // namespace

// ---------------------------------------------------------------------------
// ComponentRegistry
// ---------------------------------------------------------------------------

ComponentRegistry::ComponentRegistry() = default;

ComponentRegistry &ComponentRegistry::instance()
{
  static ComponentRegistry registry;
  return registry;
}

void ComponentRegistry::setDirectory( const QString &dir )
{
  QMutexLocker lock( &mMutex );
  mDirectory = dir;
  mLoaded = false;
}

QString ComponentRegistry::directory() const
{
  QMutexLocker lock( &mMutex );
  return mDirectory;
}

bool loadCartographyDirectory( const QString &dir, Json::Value &out, QString *error )
{
  QDir componentsDir( dir );
  if ( !componentsDir.exists() )
  {
    if ( error )
      *error = QStringLiteral( "directory does not exist: %1" ).arg( dir );
    return false;
  }
  const QFileInfoList entries =
    componentsDir.entryInfoList( QStringList() << QStringLiteral( "*.json" ), QDir::Files );
  for ( const QFileInfo &entry : entries )
  {
    Json::Value descriptor;
    QString parseError;
    if ( !parseJsonFile( entry.absoluteFilePath(), &descriptor, &parseError ) )
    {
      if ( error )
        *error = parseError;
      continue;
    }
    if ( descriptor.isObject() && descriptor.isMember( "id" ) )
      out[descriptor["id"].asString()] = descriptor;
  }
  return true;
}

void ComponentRegistry::ensureLoadedLocked() const
{
  if ( mLoaded )
    return;
  mLoaded = true;
  mComponents.clear();

  QString base = mDirectory;
  if ( base.isEmpty() )
    base = defaultComponentsDir();
  // Directory layout: <base>/components/*.json; tolerate <base> itself.
  QDir dir( base );
  const QString componentsPath =
    dir.exists( QStringLiteral( "components" ) ) ? dir.filePath( QStringLiteral( "components" ) )
                                                 : base;
  Json::Value loaded( Json::objectValue );
  loadCartographyDirectory( componentsPath, loaded, nullptr );

  if ( loaded.empty() )
  {
    const Json::Value fallback = embeddedComponents();
    for ( const auto &component : fallback )
    {
      if ( component.isObject() && component.isMember( "id" ) )
        mComponents.insert( QString::fromStdString( component["id"].asString() ), component );
    }
    return;
  }
  for ( const auto &key : loaded.getMemberNames() )
    mComponents.insert( QString::fromStdString( key ), loaded[key] );
}

Json::Value ComponentRegistry::components() const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  Json::Value list( Json::arrayValue );
  for ( auto it = mComponents.constBegin(); it != mComponents.constEnd(); ++it )
    list.append( it.value() );
  return list;
}

Json::Value ComponentRegistry::byCategory( const QString &category ) const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  Json::Value list( Json::arrayValue );
  for ( auto it = mComponents.constBegin(); it != mComponents.constEnd(); ++it )
  {
    if ( category.isEmpty() ||
         ( it.value().isMember( "category" ) &&
           it.value()["category"].asString() == category.toStdString() ) )
      list.append( it.value() );
  }
  return list;
}

Json::Value ComponentRegistry::find( const QString &id ) const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  return mComponents.value( id, Json::Value() );
}

bool ComponentRegistry::registerComponent( Json::Value descriptor, QString *error )
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  QStringList categories;
  categories << QStringLiteral( "north-arrow" ) << QStringLiteral( "scale-bar" )
             << QStringLiteral( "legend" ) << QStringLiteral( "color-bar" )
             << QStringLiteral( "title" ) << QStringLiteral( "subtitle" )
             << QStringLiteral( "grid" ) << QStringLiteral( "chart" )
             << QStringLiteral( "inset-map" ) << QStringLiteral( "annotation" )
             << QStringLiteral( "source-note" ) << QStringLiteral( "frame" );
  const auto problems = validateComponent( descriptor, categories );
  if ( !problems.empty() )
  {
    if ( error )
      *error = QString::fromStdString( problems.front() );
    return false;
  }
  mComponents.insert( QString::fromStdString( descriptor["id"].asString() ), std::move( descriptor ) );
  return true;
}

void ComponentRegistry::reload()
{
  QMutexLocker lock( &mMutex );
  mLoaded = false;
}

// ---------------------------------------------------------------------------
// TemplateRegistry
// ---------------------------------------------------------------------------

TemplateRegistry::TemplateRegistry() = default;

TemplateRegistry &TemplateRegistry::instance()
{
  static TemplateRegistry registry;
  return registry;
}

void TemplateRegistry::setDirectory( const QString &dir )
{
  QMutexLocker lock( &mMutex );
  mDirectory = dir;
  mLoaded = false;
}

QString TemplateRegistry::directory() const
{
  QMutexLocker lock( &mMutex );
  return mDirectory;
}

void TemplateRegistry::ensureLoadedLocked() const
{
  if ( mLoaded )
    return;
  mLoaded = true;
  mTemplates.clear();

  QString base = mDirectory;
  if ( base.isEmpty() )
    base = defaultComponentsDir();
  QDir dir( base );
  const QString templatesPath =
    dir.exists( QStringLiteral( "templates" ) ) ? dir.filePath( QStringLiteral( "templates" ) ) : base;
  Json::Value loaded( Json::objectValue );
  loadCartographyDirectory( templatesPath, loaded, nullptr );
  if ( loaded.empty() )
  {
    for ( const auto &tmpl : embeddedTemplates() )
      if ( tmpl.isObject() && tmpl.isMember( "id" ) )
        mTemplates.insert( QString::fromStdString( tmpl["id"].asString() ), tmpl );
    return;
  }
  for ( const auto &key : loaded.getMemberNames() )
    mTemplates.insert( QString::fromStdString( key ), loaded[key] );
}

Json::Value TemplateRegistry::templates() const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  Json::Value list( Json::arrayValue );
  for ( auto it = mTemplates.constBegin(); it != mTemplates.constEnd(); ++it )
    list.append( it.value() );
  return list;
}

Json::Value TemplateRegistry::find( const QString &id ) const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  return mTemplates.value( id, Json::Value() );
}

bool TemplateRegistry::registerTemplate( Json::Value descriptor, QString *error )
{
  if ( !descriptor.isObject() || !descriptor.isMember( "id" ) )
  {
    if ( error )
      *error = QStringLiteral( "template needs an id" );
    return false;
  }
  if ( !descriptor.isMember( "required_slots" ) || !descriptor["required_slots"].isArray() )
  {
    if ( error )
      *error = QStringLiteral( "template needs required_slots" );
    return false;
  }
  QMutexLocker lock( &mMutex );
  mTemplates.insert( QString::fromStdString( descriptor["id"].asString() ), std::move( descriptor ) );
  return true;
}

void TemplateRegistry::reload()
{
  QMutexLocker lock( &mMutex );
  mLoaded = false;
}

Json::Value TemplateRegistry::instantiateTemplate( const QString &id, const Json::Value &params,
                                                   QString *error ) const
{
  const Json::Value tmpl = find( id );
  if ( tmpl.isNull() )
  {
    if ( error )
      *error = QStringLiteral( "unknown template '%1'" ).arg( id );
    return Json::Value();
  }

  const std::string layoutName =
    params.isMember( "layout_name" ) && params["layout_name"].isString()
      ? params["layout_name"].asString()
      : id.toStdString();
  Json::Value spec = mapspec::makeMapSpec( layoutName, tmpl.get( "page", Json::Value( Json::objectValue ) ) );
  spec["template"] = id.toStdString();

  for ( const auto &slot : tmpl["required_slots"] )
  {
    Json::Value item( Json::objectValue );
    item["semantic_role"] = slot["role"];
    if ( slot.isMember( "rect_mm" ) )
      item["rect_mm"] = slot["rect_mm"];
    const std::string collection = slot["accepts"].asString();

    if ( collection == "titles" )
    {
      item["text"] = params.get( "title", "地图标题" );
    }
    else if ( collection == "source_notes" )
    {
      item["text"] = params.get( "source_note", "数据来源: SICNU GEO RS / exp-rs" );
    }
    else if ( collection == "map_frames" )
    {
      if ( params.isMember( "layers" ) && params["layers"].isArray() )
        item["layers"] = params["layers"];
      if ( params.isMember( "extent" ) && params["extent"].isArray() )
        item["extent"] = params["extent"];
    }
    else if ( collection == "legends" )
    {
      item["title"] = "图例";
    }
    else if ( collection == "scale_bars" )
    {
      item["style"] = "Single Box";
      item["units"] = "km";
    }
    else if ( collection == "colorbars" )
    {
      item["ramp"] = "sequential";
    }
    else if ( collection == "charts" )
    {
      Json::Value chart( Json::objectValue );
      chart["kind"] = "line";
      chart["binding"]["mode"] = "inline";
      chart["binding"]["data"] = Json::Value( Json::arrayValue );
      item["chart"] = chart;
    }
    mapspec::appendMapSpecItem( spec, collection, item );
  }

  // Recommended components (minimal parameterized drafts).
  if ( tmpl.isMember( "recommended_components" ) && tmpl["recommended_components"].isArray() )
  {
    ComponentRegistry &components = ComponentRegistry::instance();
    for ( const auto &ref : tmpl["recommended_components"] )
    {
      const Json::Value component = components.find( QString::fromStdString( ref.asString() ) );
      if ( component.isNull() )
        continue;
      const std::string category = component.get( "category", "" ).asString();
      const std::string collection = category == "north-arrow" ? "north_arrows"
                                     : category == "scale-bar" ? "scale_bars"
                                     : category == "legend"    ? "legends"
                                     : category == "color-bar" ? "colorbars"
                                     : category == "grid"      ? "grids"
                                     : category == "frame"     ? "constraints"
                                     : category == "chart"     ? "charts"
                                                               : "annotations";
      Json::Value item( Json::objectValue );
      item["source_component"] = component["id"];
      if ( collection == "north_arrows" )
      {
        Json::Value rect( Json::arrayValue );
        rect.append( spec["page"]["width_mm"].asDouble() - 24.0 );
        rect.append( 26.0 );
        rect.append( 12.0 );
        rect.append( 12.0 );
        item["rect_mm"] = rect;
      }
      else if ( collection == "grids" )
      {
        item["map_ref"] = spec["map_frames"].empty()
                            ? ""
                            : spec["map_frames"][0].get( "id", "" );
        item["interval"] = component["parameters"].get( "interval_deg", 1.0 );
      }
      else if ( collection == "constraints" )
      {
        item["kind"] = "frame_style";
        item["style"] = component["parameters"];
      }
      mapspec::appendMapSpecItem( spec, collection, item );
    }
  }
  return spec;
}

} // namespace sicnu::agent::cartography
