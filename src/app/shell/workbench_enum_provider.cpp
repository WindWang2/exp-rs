/***************************************************************************
 * workbench_enum_provider.cpp — Workbench 9.0 M6 production enum provider
 ***************************************************************************/
#include "workbench_enum_provider.h"

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "operators/framework/model_catalog.h"

#include <QFileInfo>

namespace sicnu::app
{

using Choice = SchemaEnumProvider::Choice;

Json::Value applyEnumSourceAnnotations( Json::Value schema )
{
  if ( !schema.isObject() )
    return schema;
  if ( schema.isMember( "properties" ) && schema["properties"].isObject() )
  {
    for ( auto &name : schema["properties"].getMemberNames() )
    {
      Json::Value &prop = schema["properties"][name];
      if ( !prop.isObject() )
        continue;
      const QString uiType =
        ( prop.isMember( "x-ui-type" ) && prop["x-ui-type"].isString() )
            ? QString::fromStdString( prop["x-ui-type"].asString() )
            : QString();
      const bool annotated = prop.isMember( "x-ui-enum-source" );
      if ( !annotated )
      {
        if ( uiType == QLatin1String( "model" ) )
          prop["x-ui-enum-source"] = "models";
        else if ( uiType == QLatin1String( "asset" ) )
          prop["x-ui-enum-source"] = "assets";
      }
      // Nested object schemas recurse; array items do not resolve enums.
      // The function takes/returns by value — write the result back.
      if ( prop.isMember( "properties" ) )
        prop = applyEnumSourceAnnotations( prop );
    }
  }
  return schema;
}

WorkbenchEnumProvider::WorkbenchEnumProvider( QObject *parent )
    : QObject( parent )
{
}

void WorkbenchEnumProvider::setRasterLayers( const QStringList &ids, const QStringList &names )
{
  m_rasterIds = ids;
  m_rasterNames = names;
}

void WorkbenchEnumProvider::setVectorLayers( const QStringList &ids, const QStringList &names )
{
  m_vectorIds = ids;
  m_vectorNames = names;
}

void WorkbenchEnumProvider::attachDataManager( sicnu::data::DataManager *manager )
{
  m_dataManager = manager;
}

QVector<Choice> WorkbenchEnumProvider::capChoices( QVector<Choice> choices,
                                                   const QString &sourceId )
{
  if ( choices.size() <= kMaxChoices )
    return choices;
  // Review B6: the truncation notice is a SENTINEL with an empty id — it
  // must never carry a selectable value (a user picking the visually-last
  // row would otherwise submit real choice #200 under the notice label).
  choices.resize( kMaxChoices - 1 );
  choices.append( { QString(),
                    QObject::tr( "…（%1 选项源已截断到前 %2 项）" )
                      .arg( sourceId )
                      .arg( kMaxChoices - 1 ) } );
  return choices;
}

QVector<Choice> WorkbenchEnumProvider::choicesFor( const QString &sourceId,
                                                   const Json::Value & )
{
  const QString source = sourceId.trimmed();

  if ( source == QLatin1String( "layers:raster" ) )
  {
    QVector<Choice> choices;
    choices.reserve( m_rasterIds.size() );
    for ( int i = 0; i < m_rasterIds.size(); ++i )
    {
      const QString name = i < m_rasterNames.size() ? m_rasterNames.at( i ) : QString();
      choices.append( { m_rasterIds.at( i ),
                        name.isEmpty() ? m_rasterIds.at( i ) : name } );
    }
    return capChoices( std::move( choices ), source );
  }

  if ( source == QLatin1String( "layers:vector" ) )
  {
    QVector<Choice> choices;
    choices.reserve( m_vectorIds.size() );
    for ( int i = 0; i < m_vectorIds.size(); ++i )
    {
      const QString name = i < m_vectorNames.size() ? m_vectorNames.at( i ) : QString();
      choices.append( { m_vectorIds.at( i ),
                        name.isEmpty() ? m_vectorIds.at( i ) : name } );
    }
    return capChoices( std::move( choices ), source );
  }

  if ( source == QLatin1String( "assets" ) )
  {
    QVector<Choice> choices;
    if ( m_dataManager )
    {
      // Snapshot read on the manager's owning thread (GUI). AssetQuery has
      // no limit field yet (data-track seam) — the read is O(catalog), but
      // label building stops at the cap and the RESOLVED list is bounded
      // (review B5).
      const auto assets = m_dataManager->assets();
      choices.reserve( qMin( assets.size(), kMaxChoices ) );
      for ( const auto &asset : assets )
      {
        if ( choices.size() >= kMaxChoices )
          break; // capChoices appends the sentinel; skip the label work
        const QString id = asset.id().toString();
        if ( id.isEmpty() )
          continue;
        const QString path = asset.source().canonicalSource;
        const QString label =
          path.isEmpty() ? id : QFileInfo( path ).fileName() + QStringLiteral( " — " ) + id;
        choices.append( { id, label } );
      }
    }
    return capChoices( std::move( choices ), source );
  }

  if ( source == QLatin1String( "models" ) )
  {
    QVector<Choice> choices;
    const auto models = sicnu::operators::ModelCatalog::instance().models();
    choices.reserve( static_cast<int>( models.size() ) );
    for ( const auto &model : models )
    {
      const QString id = QString::fromStdString( model.id );
      const QString name = QString::fromStdString( model.name );
      choices.append( { id, name.isEmpty() ? id : name } );
    }
    return capChoices( std::move( choices ), source );
  }

  // Unknown source — the 8.0 contract: empty means the form degrades to
  // editable free text instead of a dead list.
  return {};
}

} // namespace sicnu::app
