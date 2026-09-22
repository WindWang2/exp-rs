#pragma once
#include "teaching/lab_status.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QWidget>

namespace sicnu::app::teaching {

/// Text + icon status (never color-only).
inline QWidget *makeStatusBadge( sicnu::teaching::LabUiStatus status, QWidget *parent = nullptr )
{
  auto *w = new QWidget( parent );
  auto *lay = new QHBoxLayout( w );
  lay->setContentsMargins( 0, 0, 0, 0 );
  auto *icon = new QLabel( w );
  icon->setObjectName( QString::fromUtf8( sicnu::teaching::labUiStatusIconToken( status ) ) );
  icon->setText( QStringLiteral( "●" ) );
  icon->setToolTip( QString::fromUtf8( sicnu::teaching::labUiStatusIconToken( status ) ) );
  auto *text = new QLabel( QString::fromUtf8( sicnu::teaching::labUiStatusLabelZh( status ) ), w );
  text->setObjectName( QStringLiteral( "statusText" ) );
  lay->addWidget( icon );
  lay->addWidget( text );
  lay->addStretch( 1 );
  return w;
}

} // namespace sicnu::app::teaching
