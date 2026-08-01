// src/python/isolated/python_plugin_host.cpp
#include "python_plugin_host.h"

#include "python_plugin_adapter.h"
#include "python_worker_process_pool.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using namespace sicnu::python::isolated;

PythonPluginHost::PythonPluginHost( int poolSize, QObject *parent )
  : QObject( parent )
  , m_poolSize( poolSize )
{
}

PythonPluginHost::~PythonPluginHost()
{
  unloadAll();
  if ( m_pool )
  {
    m_pool->shutdown();
    delete m_pool;
    m_pool = nullptr;
  }
}

bool PythonPluginHost::ensurePool( QString *errorOut )
{
  if ( m_pool )
    return true;

  // Resolve worker_daemon.py from common layouts: installed app, source tree
  // relative to the binary, and cwd when developing from the repo root.
  const QStringList candidates = {
    QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) ),
    QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "../../src/python/scripts/worker_daemon.py" ) ),
    QDir::current().filePath( QStringLiteral( "src/python/scripts/worker_daemon.py" ) ),
    QDir::current().filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) ),
  };
  QString scriptPath;
  for ( const QString &candidate : candidates )
  {
    if ( QFileInfo::exists( candidate ) )
    {
      scriptPath = QFileInfo( candidate ).absoluteFilePath();
      break;
    }
  }
  if ( scriptPath.isEmpty() )
  {
    if ( errorOut ) *errorOut = QStringLiteral( "worker_daemon.py not found; Python plugins cannot load" );
    return false;
  }

  m_pool = new PythonWorkerProcessPool( m_poolSize, this );
  if ( !m_pool->initialize( QString(), scriptPath ) )
  {
    if ( errorOut ) *errorOut = QStringLiteral( "PythonWorkerProcessPool initialize failed: %1" ).arg( scriptPath );
    delete m_pool;
    m_pool = nullptr;
    return false;
  }
  return true;
}

PythonPluginAdapter *PythonPluginHost::loadPlugin( const QString &pluginDir,
                                                   sicnu::data::DataManager *dataManager,
                                                   QMenu *pluginMenu,
                                                   ActiveViewHost *activeViewHost,
                                                   QString *errorOut )
{
  const QString metadataPath = pluginDir + QStringLiteral( "/metadata.txt" );
  if ( !QFileInfo::exists( metadataPath ) )
  {
    if ( errorOut ) *errorOut = QStringLiteral( "No metadata.txt in %1" ).arg( pluginDir );
    return nullptr;
  }

  QMap<QString, QString> metadata;
  QFile file( metadataPath );
  if ( file.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    QTextStream in( &file );
    while ( !in.atEnd() )
    {
      QString line = in.readLine().trimmed();
      if ( line.isEmpty() || line.startsWith( '#' ) || line.startsWith( '[' ) )
        continue;
      const int idx = line.indexOf( '=' );
      if ( idx > 0 )
        metadata[line.left( idx ).trimmed()] = line.mid( idx + 1 ).trimmed();
    }
  }

  if ( !ensurePool( errorOut ) )
    return nullptr;

  const QString packageName = QDir( pluginDir ).dirName();
  const QString name = metadata.value( QStringLiteral( "name" ), packageName );
  const QString description = metadata.value( QStringLiteral( "description" ), QString() );
  const QString version = metadata.value( QStringLiteral( "version" ), QStringLiteral( "1.0" ) );

  auto adapter = std::make_unique<PythonPluginAdapter>( pluginDir, packageName, name, description, version,
                                                        dataManager, pluginMenu, activeViewHost, m_pool );
  if ( !adapter->initialize( nullptr, nullptr ) )
  {
    if ( errorOut ) *errorOut = QStringLiteral( "Python plugin initialization failed: %1" ).arg( name );
    return nullptr;
  }

  PythonPluginAdapter *raw = adapter.get();
  m_adapters.push_back( std::move( adapter ) );
  return raw;
}

void PythonPluginHost::unloadAll()
{
  for ( auto &adapter : m_adapters )
  {
    adapter->unload();
  }
  m_adapters.clear();
}

QStringList PythonPluginHost::loadedPlugins() const
{
  QStringList names;
  for ( const auto &adapter : m_adapters )
    names << adapter->name();
  return names;
}
