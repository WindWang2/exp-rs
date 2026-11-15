#pragma once

#include <QCoreApplication>
#include <QTranslator>

namespace exprs_test
{
/// Release only translators installed by the application bootstrap, leaving
/// QGIS and other fixture-owned translators untouched.
inline void removeApplicationTranslators()
{
  auto *application = QCoreApplication::instance();
  if ( !application )
    return;
  const auto translators = application->findChildren<QTranslator *>( QString(), Qt::FindDirectChildrenOnly );
  for ( auto *translator : translators )
    if ( translator->objectName() == QLatin1String( "sicnuAppTranslator" )
         || translator->objectName() == QLatin1String( "sicnuQtTranslator" ) )
    {
      application->removeTranslator( translator );
      delete translator;
    }
}
} // namespace exprs_test
