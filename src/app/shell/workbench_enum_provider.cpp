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
  choices.resize( kMaxChoices );
  choices.last().label =
    QObject::tr( "…（%1 选项源已截断到前 %2 项）" ).arg( sourceId ).arg( kMaxChoices );
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
      // Snapshot read on the manager's owning thread (GUI); bounded by cap.
      const auto assets = m_dataManager->assets();
      choices.reserve( assets.size() );
      for ( const auto &asset : assets )
      {
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
