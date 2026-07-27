// python_plugin_adapter.cpp — C++ adapter wrapping Python plugins for PluginManager
#include "python_plugin_adapter.h"
#include "sicnu_app_interface.h"
#include "qgis_python.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

PythonPluginAdapter::PythonPluginAdapter( const QString &pluginDir,
                                          const QString &packageName,
                                          const QString &name,
                                          const QString &description,
                                          const QString &version,
                                          SicnuAppInterface *appInterface )
    : m_pluginDir( pluginDir )
    , m_packageName( packageName )
    , m_name( name )
    , m_description( description )
    , m_version( version )
    , m_appInterface( appInterface )
{
    const QString iconPath = QDir( pluginDir ).filePath( QStringLiteral( "icon.png" ) );
    if ( QFileInfo::exists( iconPath ) )
    {
        m_icon = QIcon( iconPath );
    }
}

PythonPluginAdapter::~PythonPluginAdapter()
{
    if ( m_initialized )
    {
        unload();
    }
}

bool PythonPluginAdapter::initialize( QgsMapCanvas *canvas, QgsLayerTreeView *layerTree )
{
    Q_UNUSED( canvas );
    Q_UNUSED( layerTree );

    if ( m_initialized )
        return true;

    if ( !QgisPython::instance().initialize() )
    {
        qWarning() << "PythonPluginAdapter: Failed to initialize Python engine";
        return false;
    }

    // Add plugin parent directory to sys.path
    QDir dir( m_pluginDir );
    dir.cdUp();
    QgisPython::instance().addPath( dir.absolutePath() );

    QString error;

    // Standard QGIS plugin loading contract: import module, invoke classFactory(iface), call initGui()
    QString script = QString( R"(
import importlib
import sys

_mod = importlib.import_module('%1')
if hasattr(_mod, 'classFactory'):
    _plugin = _mod.classFactory(None)
    if hasattr(_plugin, 'initGui'):
        _plugin.initGui()
)" ).arg( m_packageName );

    bool ok = QgisPython::instance().runString( script, error );
    if ( !ok )
    {
        qWarning() << "Failed to start Python plugin:" << m_packageName << error;
        return false;
    }

    m_initialized = true;
    return true;
}

void PythonPluginAdapter::unload()
{
    if ( !m_initialized )
        return;

    QString error;
    QString script = QString( R"(
import sys
if '%1' in sys.modules:
    _mod = sys.modules['%1']
    if hasattr(_mod, '_plugin') and hasattr(_mod._plugin, 'unload'):
        _mod._plugin.unload()
)" ).arg( m_packageName );

    QgisPython::instance().runString( script, error );
    m_initialized = false;
}
