// sicnu_python_runner.cpp — SICNU GEO RS QgsPythonRunner integration
#include "sicnu_python_runner.h"
#include "qgis_python.h"

#include <QDebug>

bool SicnuPythonRunner::runCommand( QString command, QString messageOnError )
{
  Q_UNUSED( messageOnError );
  QString error;
  return QgisPython::instance().runString( command, error );
}

bool SicnuPythonRunner::runFileCommand( const QString &filename, const QString &messageOnError )
{
  Q_UNUSED( messageOnError );
  QString error;
  return QgisPython::instance().runFile( filename, error );
}

bool SicnuPythonRunner::evalCommand( QString command, QString &result )
{
  QString error;
  return QgisPython::instance().evalString( command, result, error );
}

bool SicnuPythonRunner::setArgvCommand( const QStringList &arguments, const QString &messageOnError )
{
  Q_UNUSED( messageOnError );
  QString cmd = QStringLiteral( "import sys\nsys.argv = [" );
  for ( int i = 0; i < arguments.size(); ++i )
  {
    if ( i > 0 )
      cmd += QStringLiteral( ", " );
    cmd += QStringLiteral( "'%1'" ).arg( arguments.at( i ) );
  }
  cmd += QStringLiteral( "]" );
  QString error;
  return QgisPython::instance().runString( cmd, error );
}
