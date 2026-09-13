/***************************************************************************
 * plugin_command_defs.cpp — Workbench 9.0 M8 plugin command registration
 ***************************************************************************/
#include "plugin_command_defs.h"

#include "command_registry.h"

#include <QPointer>
#include <QAction>

namespace sicnu::app
{

int registerPluginMenuCommands( CommandRegistry *registry,
                                const QList<QAction *> &actions,
                                const QString &pluginId,
                                const QString &categoryLabel )
{
  if ( !registry )
    return 0;
  int registered = 0;
  int index = 0;
  for ( QAction *action : actions )
  {
    if ( !action )
      continue;
    CommandDefinition d;
    d.id = QStringLiteral( "plugin.%1.%2" ).arg( pluginId, QString::number( index ) );
    d.title = action->text();
    d.title.remove( QLatin1Char( '&' ) );
    d.description = QObject::tr( "Features provided by plugin %1." ).arg( pluginId );
    d.category = categoryLabel;
    const QPointer<QAction> guard( action );
    d.availability = [guard]( const SelectionContextSnapshot & ) {
      return !guard.isNull();
    };
    d.handler = [guard] {
      if ( guard )
        guard->trigger();
    };
    if ( registry->registerCommand( d ) )
      ++registered;
    ++index;
  }
  return registered;
}

} // namespace sicnu::app
