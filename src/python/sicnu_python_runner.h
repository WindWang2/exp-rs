// sicnu_python_runner.h — SICNU GEO RS QgsPythonRunner integration
#pragma once

#include "qgspythonrunner.h"

/**
 * Concrete implementation of QgsPythonRunner that delegates execution
 * to QgisPython (the embedded Python interpreter singleton).
 */
class SicnuPythonRunner : public QgsPythonRunner
{
public:
  SicnuPythonRunner() = default;
  ~SicnuPythonRunner() override = default;

protected:
  bool runCommand( QString command, QString messageOnError = QString() ) override;
  bool runFileCommand( const QString &filename, const QString &messageOnError = QString() ) override;
  bool evalCommand( QString command, QString &result ) override;
  bool setArgvCommand( const QStringList &arguments, const QString &messageOnError = QString() ) override;
};
