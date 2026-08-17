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

#include <QJsonArray>
#include <QJsonDocument>

bool SicnuPythonRunner::setArgvCommand( const QStringList &arguments, const QString &messageOnError )
{
  Q_UNUSED( messageOnError );
  const QJsonArray arr = QJsonArray::fromStringList( arguments );
  const QString jsonStr = QString::fromUtf8( QJsonDocument( arr ).toJson( QJsonDocument::Compact ) );
  QString cmd = QStringLiteral( "import sys\nsys.argv = " ) + jsonStr;
  QString error;
  return QgisPython::instance().runString( cmd, error );
}
