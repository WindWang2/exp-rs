// dialog_utils.cpp — Shared utilities for dialog UI chrome
#include "dialog_utils.h"

#include <QComboBox>
#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariant>

#include <qgsproject.h>
#include <raster/qgsrasterlayer.h>

void populateRasterLayerCombo( QComboBox *combo, bool clearFirst )
{
  if ( !combo )
    return;

  if ( clearFirst )
    combo->clear();

  const auto layers = QgsProject::instance()->mapLayers();
  for ( auto it = layers.constBegin(); it != layers.constEnd(); ++it )
  {
    auto *rl = qobject_cast<QgsRasterLayer *>( it.value() );
    if ( rl && rl->isValid() )
      combo->addItem( rl->name(), QVariant::fromValue( rl ) );
  }
}

namespace SicnuUi
{

void polishDialog( QDialog *dlg, int minWidth )
{
  if ( !dlg )
    return;
  dlg->setObjectName( QStringLiteral( "rsToolDialog" ) );
  if ( dlg->minimumWidth() < minWidth )
    dlg->setMinimumWidth( minWidth );
  dlg->setSizeGripEnabled( true );
}

QVBoxLayout *makeDialogRootLayout( QWidget *host )
{
  auto *root = new QVBoxLayout( host );
  root->setContentsMargins( 14, 12, 14, 12 );
  root->setSpacing( 12 );
  return root;
}

QFrame *makeSection( QWidget *parent, const QString &title, const QString &tip )
{
  auto *frame = new QFrame( parent );
  frame->setObjectName( QStringLiteral( "rsDialogSection" ) );
  frame->setFrameShape( QFrame::StyledPanel );
  auto *lay = new QVBoxLayout( frame );
  lay->setContentsMargins( 12, 10, 12, 12 );
  lay->setSpacing( 8 );

  auto *header = new QLabel( title, frame );
  header->setObjectName( QStringLiteral( "rsDialogSectionTitle" ) );
  QFont f = header->font();
  f.setBold( true );
  f.setPointSizeF( f.pointSizeF() + 0.5 );
  header->setFont( f );
  if ( !tip.isEmpty() )
  {
    header->setToolTip( tip );
    header->setWhatsThis( tip );
    header->setStatusTip( tip );
    frame->setToolTip( tip );
  }
  lay->addWidget( header );
  return frame;
}

QGroupBox *makeGroup( QWidget *parent, const QString &title )
{
  auto *g = new QGroupBox( title, parent );
  g->setObjectName( QStringLiteral( "rsDialogGroup" ) );
  return g;
}

QLabel *makeHintLabel( QWidget *parent, const QString &text )
{
  auto *lbl = new QLabel( text, parent );
  lbl->setObjectName( QStringLiteral( "rsDialogHint" ) );
  lbl->setWordWrap( true );
  lbl->setObjectName( QStringLiteral( "rsDialogHint" ) );
  return lbl;
}

void markPrimary( QPushButton *btn )
{
  if ( !btn )
    return;
  btn->setObjectName( QStringLiteral( "rsPrimaryButton" ) );
  btn->setProperty( "primary", true );
  btn->setDefault( true );
  btn->setMinimumHeight( 32 );
  btn->setMinimumWidth( 96 );
  btn->style()->unpolish( btn );
  btn->style()->polish( btn );
}

void markSecondary( QPushButton *btn )
{
  if ( !btn )
    return;
  btn->setObjectName( QStringLiteral( "rsSecondaryButton" ) );
  btn->setProperty( "ghost", true );
  btn->setMinimumHeight( 32 );
  btn->style()->unpolish( btn );
  btn->style()->polish( btn );
}

QHBoxLayout *makeActionRow( QWidget *parent )
{
  Q_UNUSED( parent );
  auto *row = new QHBoxLayout();
  row->setContentsMargins( 0, 4, 0, 0 );
  row->setSpacing( 8 );
  return row;
}

} // namespace SicnuUi
