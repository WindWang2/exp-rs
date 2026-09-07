// src/agent/cartography/registry.cpp
#include "registry.h"

#include "../mapspec/mapspec.h"
#include "design_tokens.h"
#include "solution_registry.h"
#include "style_spec.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMutexLocker>

#include <json/reader.h>

#include <set>
#include <sstream>
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
  std::istringstream stream( bytes.toStdString() );
  Json::CharReaderBuilder builder;
  std::string parseErrors;
  if ( !Json::parseFromStream( builder, stream, out, &parseErrors ) )
  {
    if ( error )
      *error = QStringLiteral( "%1: %2" ).arg( path, QString::fromStdString( parseErrors ) );
    return false;
  }
  return true;
}

/// Single category→collection table powering validation (knownCategories)
/// and instantiation (collectionForCategory) so the two can never drift.
struct CategoryInfo
{
    const char *category;
    const char *collection;
};

const CategoryInfo kCategoryInfos[] = {
  { "north-arrow", "north_arrows" }, { "scale-bar", "scale_bars" },
  { "legend", "legends" },           { "color-bar", "colorbars" },
  { "title", "titles" },             { "subtitle", "titles" },
  { "text", "labels" },              { "grid", "grids" },
  { "chart", "charts" },             { "map-frame", "map_frames" },
  { "inset-map", "inset_maps" },     { "annotation", "annotations" },
  { "source-note", "source_notes" }, { "frame", "constraints" },
  { "publication", "labels" },
};

QStringList knownCategories()
{
  QStringList categories;
  for ( const auto &info : kCategoryInfos )
    categories << QString::fromLatin1( info.category );
  return categories;
}

/// Shape check for one `variants[]` entry.
void checkVariant( const std::string &id, int index, const Json::Value &variant,
                   std::vector<std::string> &problems )
{
  const std::string where = id + ": variants[" + std::to_string( index ) + "]";
  if ( !variant.isObject() )
  {
    problems.push_back( where + " must be an object" );
    return;
  }
  if ( !variant.isMember( "id" ) || !variant["id"].isString() || variant["id"].asString().empty() )
    problems.push_back( where + " needs a string id" );
  for ( const char *member : { "parameters", "defaults" } )
    if ( variant.isMember( member ) && !variant[member].isObject() )
      problems.push_back( where + "." + member + " must be an object" );
}

/// Descriptor-level validation shared by the loader and registerComponent.
std::vector<std::string> validateComponent( const Json::Value &descriptor,
                                            const QStringList &knownCategories_ )
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
  else if ( !knownCategories_.contains( QString::fromStdString( descriptor["category"].asString() ) ) )
    problems.push_back( id + ": unknown category '" + descriptor["category"].asString() + "'" );
  if ( descriptor.isMember( "parameters" ) && !descriptor["parameters"].isObject() )
    problems.push_back( id + ": parameters must be an object" );
  if ( descriptor.isMember( "layout_constraints" ) && !descriptor["layout_constraints"].isObject() )
    problems.push_back( id + ": layout_constraints must be an object" );
  // --- schema v2 (Design System 4.0) ---------------------------------------
  if ( descriptor.isMember( "version" ) && !descriptor["version"].isIntegral() )
    problems.push_back( id + ": version must be an integer" );
  if ( descriptor.isMember( "semantic_roles" ) && !descriptor["semantic_roles"].isArray() )
    problems.push_back( id + ": semantic_roles must be an array" );
  if ( descriptor.isMember( "variants" ) )
  {
    if ( !descriptor["variants"].isArray() )
      problems.push_back( id + ": variants must be an array" );
    else
    {
      std::set<std::string> variantIds;
      for ( int index = 0; index < static_cast<int>( descriptor["variants"].size() ); ++index )
      {
        const Json::Value &variant = descriptor["variants"][index];
        checkVariant( id, index, variant, problems );
        if ( variant.isObject() && variant.isMember( "id" ) && variant["id"].isString() &&
             !variantIds.insert( variant["id"].asString() ).second )
          problems.push_back( id + ": duplicate variant id '" + variant["id"].asString() + "'" );
      }
    }
  }
  for ( const char *member : { "defaults", "validation", "compatibility" } )
    if ( descriptor.isMember( member ) && !descriptor[member].isObject() )
      problems.push_back( id + ": " + member + " must be an object" );
  if ( descriptor.isMember( "data_bindings" ) )
  {
    if ( !descriptor["data_bindings"].isArray() )
      problems.push_back( id + ": data_bindings must be an array" );
    else
      for ( const auto &binding : descriptor["data_bindings"] )
        if ( !binding.isObject() || !binding.isMember( "name" ) || !binding["name"].isString() )
          problems.push_back( id + ": every data_binding needs a string name" );
  }
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
  Json::Value slotList( Json::arrayValue );
  const auto appendSlot = [ &slotList ]( const char *role, const char *collection, double x,
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
    slotList.append( s );
  };
  appendSlot( "map.main", "map_frames", 12, 24, 190, 160 );
  appendSlot( "title.main", "titles", 12, 6, 200, 14 );
  appendSlot( "colorbar.primary", "colorbars", 60, 190, 90, 8 );
  appendSlot( "scalebar.primary", "scale_bars", 14, 188, 60, 8 );
  appendSlot( "source.primary", "source_notes", 170, 190, 110, 8 );
  tmpl["required_slots"] = slotList;
  list.append( tmpl );
  return list;
}

} // namespace

std::vector<std::string> validateComponentDescriptor( const Json::Value &descriptor )
{
  return validateComponent( descriptor, knownCategories() );
}

std::string collectionForCategory( const std::string &category )
{
  for ( const auto &info : kCategoryInfos )
    if ( category == info.category )
      return info.collection;
  return std::string();
}

Json::Value resolveComponent( const QString &id, const QString &variant )
{
  ComponentRegistry &registry = ComponentRegistry::instance();
  Json::Value descriptor = registry.find( id );
  if ( descriptor.isNull() || variant.isEmpty() )
    return descriptor;
  if ( !descriptor.isMember( "variants" ) || !descriptor["variants"].isArray() )
    return Json::Value(); // unknown variant
  for ( const auto &candidate : descriptor["variants"] )
  {
    if ( candidate.isObject() && candidate.isMember( "id" ) &&
         candidate["id"].asString() == variant.toStdString() )
    {
      if ( candidate.isMember( "parameters" ) && candidate["parameters"].isObject() )
        descriptor["parameters"] =
          mergeTokenValues( descriptor.get( "parameters", Json::Value( Json::objectValue ) ),
                            candidate["parameters"] );
      if ( candidate.isMember( "defaults" ) && candidate["defaults"].isObject() )
        descriptor["defaults"] =
          mergeTokenValues( descriptor.get( "defaults", Json::Value( Json::objectValue ) ),
                            candidate["defaults"] );
      return descriptor;
    }
  }
  return Json::Value(); // unknown variant
}

bool applyComponentDefaults( Json::Value &item, QString *error )
{
  if ( !item.isObject() || !item.isMember( "source_component" ) )
    return true;
  QString id;
  QString variant;
  const Json::Value &reference = item["source_component"];
  if ( reference.isString() )
  {
    id = QString::fromStdString( reference.asString() );
  }
  else if ( reference.isObject() && reference.isMember( "id" ) && reference["id"].isString() )
  {
    id = QString::fromStdString( reference["id"].asString() );
    if ( reference.isMember( "variant" ) && reference["variant"].isString() )
      variant = QString::fromStdString( reference["variant"].asString() );
  }
  else
  {
    if ( error )
      *error = QStringLiteral( "source_component must be an id string or {id, variant}" );
    return false;
  }
  const Json::Value descriptor = resolveComponent( id, variant );
  if ( descriptor.isNull() )
  {
    if ( error )
      *error = QStringLiteral( "unknown component '%1'%2" )
                 .arg( id, variant.isEmpty() ? QString()
                                             : QStringLiteral( " (variant '%1')" ).arg( variant ) );
    return false;
  }
  // Precedence: item explicit > variant parameters > component defaults.
  // Variant parameters flow through the resolved descriptor's parameters.
  const Json::Value defaults = descriptor.isMember( "defaults" ) && descriptor["defaults"].isObject()
                                 ? mergeTokenValues( descriptor["defaults"],
                                                     descriptor.get( "parameters",
                                                                     Json::Value( Json::objectValue ) ) )
                                 : descriptor.get( "parameters", Json::Value( Json::objectValue ) );
  for ( const auto &key : defaults.getMemberNames() )
  {
    if ( item.isMember( key ) )
      continue; // explicit item fields always win
    item[key] = defaults[key];
  }
  return true;
}

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
  mLoadProblems.clear();

  QString base = mDirectory;
  if ( base.isEmpty() )
    base = defaultComponentsDir();
  // Directory layout: <base>/components/*.json; tolerate <base> itself.
  QDir dir( base );
  const QString componentsPath =
    dir.exists( QStringLiteral( "components" ) ) ? dir.filePath( QStringLiteral( "components" ) )
                                                 : base;
  Json::Value loaded( Json::objectValue );
  const QFileInfoList entries =
    QDir( componentsPath ).entryInfoList( QStringList() << QStringLiteral( "*.json" ), QDir::Files );
  for ( const QFileInfo &entry : entries )
  {
    Json::Value doc;
    QString parseError;
    if ( !parseJsonFile( entry.absoluteFilePath(), &doc, &parseError ) )
    {
      mLoadProblems << parseError;
      continue;
    }
    const auto problems = validateComponent( doc, knownCategories() );
    if ( doc.isObject() && doc.isMember( "id" ) && problems.empty() )
      mComponents.insert( QString::fromStdString( doc["id"].asString() ), doc );
    else
      mLoadProblems << QString::fromStdString( entry.fileName().toStdString() + ": " +
                                               ( problems.empty() ? "missing id"
                                                                  : problems.front() ) );
  }

  if ( mComponents.isEmpty() )
  {
    const Json::Value fallback = embeddedComponents();
    for ( const auto &component : fallback )
    {
      if ( component.isObject() && component.isMember( "id" ) )
        mComponents.insert( QString::fromStdString( component["id"].asString() ), component );
    }
    return;
  }
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
  const auto problems = validateComponent( descriptor, knownCategories() );
  if ( !problems.empty() )
  {
    if ( error )
      *error = QString::fromStdString( problems.front() );
    return false;
  }
  mComponents.insert( QString::fromStdString( descriptor["id"].asString() ), std::move( descriptor ) );
  return true;
}

QStringList ComponentRegistry::loadProblems() const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  return mLoadProblems;
}

void ComponentRegistry::reload()
{
  QMutexLocker lock( &mMutex );
  mLoaded = false;
}

// ---------------------------------------------------------------------------
// TemplateRegistry
// ---------------------------------------------------------------------------

/// Resolves one template's `extends` chain against the raw descriptor table.
Json::Value resolveTemplateChain( const Json::Value &raw, const QString &id, QString *problem,
                                  int depth = 0 );

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
  mLoadProblems.clear();

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
  // Resolve `extends` chains deterministically (Design System 4.0): the
  // resolved descriptor merges the parent chain beneath the child.
  for ( const auto &key : loaded.getMemberNames() )
  {
    const QString qkey = QString::fromStdString( key );
    QString problem;
    const Json::Value resolved = resolveTemplateChain( loaded, qkey, &problem );
    if ( resolved.isNull() || !problem.isEmpty() )
      mLoadProblems << QStringLiteral( "%1: %2" ).arg( qkey, problem );
    if ( !resolved.isNull() )
      mTemplates.insert( qkey, resolved );
  }
}

/// Deep-merges parent slots/components beneath the child: child slots win by
/// `role`; other parent slots are inherited; recommended_components and
/// suitable_tasks concatenate (parent first, duplicates dropped).
Json::Value resolveTemplateChain( const Json::Value &raw, const QString &id, QString *problem,
                                  int depth )
{
  if ( depth > 8 )
  {
    if ( problem )
      *problem = QStringLiteral( "extends chain too deep (cycle?)" );
    return Json::Value();
  }
  if ( !raw.isMember( id.toStdString() ) )
  {
    if ( problem )
      *problem = QStringLiteral( "unknown template '%1'" ).arg( id );
    return Json::Value();
  }
  const Json::Value &self = raw[id.toStdString()];
  if ( !self.isObject() )
  {
    if ( problem )
      *problem = QStringLiteral( "template '%1' is not an object" ).arg( id );
    return Json::Value();
  }
  const std::string parent =
    self.isMember( "extends" ) && self["extends"].isString() ? self["extends"].asString() : "";
  if ( parent.empty() || parent == id.toStdString() )
    return self;
  const Json::Value base = resolveTemplateChain( raw, QString::fromStdString( parent ), problem,
                                                 depth + 1 );
  if ( base.isNull() )
    return Json::Value();

  Json::Value merged = base;
  for ( const auto &key : self.getMemberNames() )
  {
    if ( key == "extends" )
      continue;
    if ( key == "slots" || key == "required_slots" )
    {
      // Merge by role: parent slots provide defaults; child slots with the
      // same role replace them entirely. Mixed-vocabulary chains
      // (v1 required_slots parent, v2 slots child) are normalized.
      const char *parentKey = base.isMember( "slots" ) && base["slots"].isArray()
                                ? "slots"
                                : ( base.isMember( "required_slots" ) &&
                                        base["required_slots"].isArray()
                                      ? "required_slots"
                                      : nullptr );
      const Json::Value &parentSlots =
        parentKey ? base[parentKey] : Json::Value( Json::arrayValue );
      Json::Value combined( Json::arrayValue );
      std::set<std::string> childRoles;
      for ( const auto &slot : self[key] )
        if ( slot.isObject() && slot.isMember( "role" ) )
          childRoles.insert( slot["role"].asString() );
      for ( const auto &slot : parentSlots )
        if ( slot.isObject() && slot.isMember( "role" ) &&
             !childRoles.count( slot["role"].asString() ) )
          combined.append( slot );
      for ( const auto &slot : self[key] )
        combined.append( slot );
      merged[key] = combined;
    }
    else if ( key == "recommended_components" || key == "suitable_tasks" )
    {
      Json::Value combined( Json::arrayValue );
      std::set<std::string> seen;
      const Json::Value &parentList = base.isMember( key ) && base[key].isArray()
                                        ? base[key]
                                        : Json::Value( Json::arrayValue );
      for ( const auto &entry : parentList )
      {
        const std::string identity = entry.isString() ? entry.asString()
                                                      : entry.get( "id", "" ).asString();
        if ( seen.insert( identity ).second )
          combined.append( entry );
      }
      for ( const auto &entry : self[key] )
      {
        const std::string identity = entry.isString() ? entry.asString()
                                                      : entry.get( "id", "" ).asString();
        if ( seen.insert( identity ).second )
          combined.append( entry );
      }
      merged[key] = combined;
    }
    else if ( key == "style" )
    {
      merged[key] = mergeTokenValues( base.get( "style", Json::Value( Json::objectValue ) ),
                                      self[key].isObject() ? self[key]
                                                           : Json::Value( Json::objectValue ) );
    }
    else
    {
      merged[key] = self[key];
    }
  }
  // stamp resolved inheritance for the gallery + debugging
  merged["extends"] = parent;
  return merged;
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
  const bool hasSlots = ( descriptor.isMember( "slots" ) && descriptor["slots"].isArray() ) ||
                        ( descriptor.isMember( "required_slots" ) &&
                          descriptor["required_slots"].isArray() );
  if ( !hasSlots )
  {
    if ( error )
      *error = QStringLiteral( "template needs slots (or required_slots)" );
    return false;
  }
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked(); // a pre-load registration must survive the lazy load
  // Programmatic templates are stored as provided (no extends resolution —
  // only the disk catalog resolves inheritance).
  mTemplates.insert( QString::fromStdString( descriptor["id"].asString() ), std::move( descriptor ) );
  return true;
}

QStringList TemplateRegistry::loadProblems() const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  return mLoadProblems;
}

void TemplateRegistry::reload()
{
  QMutexLocker lock( &mMutex );
  mLoaded = false;
}

// ---------------------------------------------------------------------------
// Catalog index (Design System 4.0): generated machine index behind the
// gallery docs and the docs-drift test.
// ---------------------------------------------------------------------------

Json::Value buildCatalogIndex()
{
  Json::Value out( Json::objectValue );
  out["kind"] = "cartography_catalog_index";
  out["index_version"] = 1;

  Json::Value tokens( Json::arrayValue );
  for ( const auto &set : TokenSetRegistry::instance().tokenSets() )
  {
    Json::Value entry( Json::objectValue );
    entry["id"] = set["id"];
    entry["version"] = set["version"];
    entry["description"] = set["description"];
    if ( set.isMember( "variants" ) && set["variants"].isObject() )
    {
      Json::Value mediums( Json::arrayValue );
      mediums.append( "print" ); // print is always resolvable (base document)
      for ( const auto &medium : set["variants"].getMemberNames() )
        if ( medium != "print" )
          mediums.append( medium );
      entry["mediums"] = mediums;
    }
    tokens.append( entry );
  }
  out["token_sets"] = tokens;

  Json::Value components( Json::arrayValue );
  for ( const auto &descriptor : ComponentRegistry::instance().components() )
  {
    Json::Value entry( Json::objectValue );
    entry["id"] = descriptor["id"];
    entry["version"] = descriptor.get( "version", 1 );
    entry["category"] = descriptor["category"];
    entry["description"] = descriptor["description"];
    if ( descriptor.isMember( "semantic_roles" ) )
      entry["semantic_roles"] = descriptor["semantic_roles"];
    if ( descriptor.isMember( "variants" ) )
    {
      Json::Value variants( Json::arrayValue );
      for ( const auto &variant : descriptor["variants"] )
        variants.append( variant["id"] );
      entry["variants"] = variants;
    }
    if ( descriptor.isMember( "compatibility" ) )
      entry["compatibility"] = descriptor["compatibility"];
    components.append( entry );
  }
  out["components"] = components;

  Json::Value templates( Json::arrayValue );
  for ( const auto &tmpl : TemplateRegistry::instance().templates() )
  {
    Json::Value entry( Json::objectValue );
    entry["id"] = tmpl["id"];
    entry["description"] = tmpl["description"];
    if ( tmpl.isMember( "extends" ) )
      entry["extends"] = tmpl["extends"];
    if ( tmpl.isMember( "product_type" ) )
      entry["product_type"] = tmpl["product_type"];
    if ( tmpl.isMember( "page" ) )
    {
      Json::Value page( Json::objectValue );
      page["width_mm"] = tmpl["page"]["width_mm"];
      page["height_mm"] = tmpl["page"]["height_mm"];
      entry["page"] = page;
    }
    if ( tmpl.isMember( "style" ) && tmpl["style"].isObject() )
      entry["style"] = tmpl["style"];
    if ( tmpl.isMember( "suitable_tasks" ) )
      entry["suitable_tasks"] = tmpl["suitable_tasks"];
    const Json::Value &slotList = tmpl.isMember( "slots" ) && tmpl["slots"].isArray()
                                    ? tmpl["slots"]
                                    : tmpl["required_slots"];
    if ( slotList.isArray() )
    {
      Json::Value roles( Json::arrayValue );
      for ( const auto &slot : slotList )
        if ( slot.isObject() && slot.isMember( "role" ) )
          roles.append( slot["role"] );
      entry["slot_roles"] = roles;
    }
    templates.append( entry );
  }
  out["templates"] = templates;

  // Platform 5.0: style specs and solution templates join the machine index
  // so the drift test covers the full knowledge platform.
  Json::Value styles( Json::arrayValue );
  for ( const auto &style : StyleRegistry::instance().styles() )
    styles.append( compactStyleSummary( style ) );
  out["styles"] = styles;

  Json::Value solutions( Json::arrayValue );
  for ( const auto &solution : SolutionRegistry::instance().solutions() )
    solutions.append( compactSolutionSummary( solution ) );
  out["solutions"] = solutions;
  return out;
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
  // Template style (token set + medium) travels into the draft.
  if ( tmpl.isMember( "style" ) && tmpl["style"].isObject() )
    spec["style"] = tmpl["style"];

  // Slots may be declared as `slots` (v2) or `required_slots` (v1 compat).
  // NB: the local is not named `slots` — Qt's moc keyword macro would eat it.
  const Json::Value emptyArray = Json::Value( Json::arrayValue );
  const Json::Value slotList = tmpl.isMember( "slots" ) && tmpl["slots"].isArray()
                                 ? tmpl["slots"]
                                 : ( tmpl.isMember( "required_slots" ) &&
                                         tmpl["required_slots"].isArray()
                                       ? tmpl["required_slots"]
                                       : emptyArray );
  // Per-slot parameter overrides: params.slots = {<role>: {…item fields…}}.
  const Json::Value slotParams =
    params.isMember( "slots" ) && params["slots"].isObject() ? params["slots"] : Json::Value();

  for ( const auto &slot : slotList )
  {
    if ( !slot.isObject() || !slot.isMember( "role" ) || !slot.isMember( "accepts" ) )
      continue;
    Json::Value item( Json::objectValue );
    item["semantic_role"] = slot["role"];
    if ( slot.isMember( "rect_mm" ) )
      item["rect_mm"] = slot["rect_mm"];
    const std::string collection = slot["accepts"].asString();

    // Slot content draft (v2): item-shaped fields deep-merged under the role.
    if ( slot.isMember( "content" ) && slot["content"].isObject() )
      item = mergeTokenValues( item, slot["content"] );
    // Slot component reference supplies defaults as well.
    if ( slot.isMember( "component" ) )
      item["source_component"] = slot["component"];

    // Conventional parameter bindings (v1 compat surface).
    if ( collection == "titles" && !item.isMember( "text" ) )
      item["text"] = params.get( "title", "地图标题" );
    else if ( collection == "source_notes" && !item.isMember( "text" ) )
      item["text"] = params.get( "source_note", "数据来源: SICNU GEO RS / exp-rs" );
    else if ( collection == "legends" && !item.isMember( "title" ) )
      item["title"] = "图例";
    else if ( collection == "scale_bars" )
    {
      if ( !item.isMember( "style" ) )
        item["style"] = "Single Box";
      if ( !item.isMember( "units" ) )
        item["units"] = "km";
    }
    else if ( collection == "colorbars" && !item.isMember( "ramp" ) )
      item["ramp"] = "sequential";
    else if ( collection == "map_frames" )
    {
      if ( params.isMember( "layers" ) && params["layers"].isArray() && !item.isMember( "layers" ) )
        item["layers"] = params["layers"];
      if ( params.isMember( "extent" ) && params["extent"].isArray() && !item.isMember( "extent" ) )
        item["extent"] = params["extent"];
    }

    // Per-slot agent overrides win over everything above.
    if ( slotParams.isMember( slot["role"].asString() ) &&
         slotParams[slot["role"].asString()].isObject() )
      item = mergeTokenValues( item, slotParams[slot["role"].asString()] );

    // Resolve component defaults last (they never override explicit fields).
    applyComponentDefaults( item, nullptr );
    mapspec::appendMapSpecItem( spec, collection, item );
  }

  // Recommended components: string id or {id, variant}.
  if ( tmpl.isMember( "recommended_components" ) && tmpl["recommended_components"].isArray() )
  {
    for ( const auto &ref : tmpl["recommended_components"] )
    {
      QString componentId;
      QString variantId;
      if ( ref.isString() )
        componentId = QString::fromStdString( ref.asString() );
      else if ( ref.isObject() && ref.isMember( "id" ) && ref["id"].isString() )
      {
        componentId = QString::fromStdString( ref["id"].asString() );
        if ( ref.isMember( "variant" ) && ref["variant"].isString() )
          variantId = QString::fromStdString( ref["variant"].asString() );
      }
      const Json::Value component = resolveComponent( componentId, variantId );
      if ( component.isNull() )
        continue;
      const std::string category = component.get( "category", "" ).asString();
      // Map frames come from slots, never from recommended components.
      if ( category == "map-frame" )
        continue;
      const std::string collection = collectionForCategory( category );
      if ( collection.empty() )
        continue;
      Json::Value item( Json::objectValue );
      item["source_component"] = ref;
      if ( collection == "north_arrows" )
      {
        // Classic placement: inside the first map frame's top-right corner
        // (north arrows are overlay furniture; map frames are not overlap
        // obstacles). Without a map frame, fall back to the top-right margin
        // strip below the title band.
        double size = 12.0;
        const Json::Value sizeParam = component.get( "parameters", Json::Value( Json::objectValue ) )
                                        .get( "size_mm", Json::Value() );
        if ( sizeParam.isNumeric() )
          size = sizeParam.asDouble();
        Json::Value rect( Json::arrayValue );
        if ( spec.isMember( "map_frames" ) && spec["map_frames"].isArray() &&
             !spec["map_frames"].empty() && spec["map_frames"][0].isMember( "rect_mm" ) &&
             spec["map_frames"][0]["rect_mm"].isArray() &&
             spec["map_frames"][0]["rect_mm"].size() == 4 )
        {
          const Json::Value &frameRect = spec["map_frames"][0]["rect_mm"];
          rect.append( frameRect[0].asDouble() + frameRect[2].asDouble() - size - 4.0 );
          rect.append( frameRect[1].asDouble() + 4.0 );
        }
        else
        {
          rect.append( spec["page"]["width_mm"].asDouble() - size - 4.0 );
          rect.append( 6.0 );
        }
        rect.append( size );
        rect.append( size );
        item["rect_mm"] = rect;
      }
      else if ( collection == "grids" )
      {
        item["map_ref"] = spec["map_frames"].empty()
                            ? ""
                            : spec["map_frames"][0].get( "id", "" );
      }
      else if ( collection == "constraints" )
      {
        item["kind"] = "frame_style";
        item["style"] = component["parameters"];
      }
      applyComponentDefaults( item, nullptr );
      // Templates that already place this furniture via slots (same
      // semantic_role) win — a recommended component must not duplicate it.
      const std::string role = item.get( "semantic_role", "" ).asString();
      bool roleTaken = false;
      if ( !role.empty() && spec.isMember( collection ) && spec[collection].isArray() )
      {
        for ( const auto &existing : spec[collection] )
          roleTaken = roleTaken ||
                      ( existing.isObject() &&
                        existing.get( "semantic_role", "" ).asString() == role );
      }
      if ( roleTaken )
        continue;
      mapspec::appendMapSpecItem( spec, collection, item );
    }
  }
  return spec;
}

} // namespace sicnu::agent::cartography
