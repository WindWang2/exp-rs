#include "rs_georef_mode_toggle.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStringList>

RsGeorefModeToggle::RsGeorefModeToggle( QWidget *parent )
  : QWidget( parent )
  , mGroup( new QButtonGroup( this ) )
{
  setObjectName( QStringLiteral( "rsModeToggle" ) );

  auto *lay = new QHBoxLayout( this );
  lay->setContentsMargins( 2, 2, 2, 2 );
  lay->setSpacing( 1 );

  mGroup->setExclusive( true );

  const QStringList labels = {
    tr( "Image → Map" ),
    tr( "Image → Image" ),
    tr( "RPC 物理模型" )
  };

  for ( int i = 0; i < labels.size(); ++i )
  {
    auto *btn = new QPushButton( labels[i], this );
    btn->setCheckable( true );
    btn->setObjectName( QStringLiteral( "rsModeBtn" ) );
    btn->setProperty( "modeIndex", i );
    if ( i == 0 )
      btn->setChecked( true );
    mGroup->addButton( btn, i );
    lay->addWidget( btn );
  }

  setStyleSheet( QStringLiteral(
    "#rsModeToggle { background: #f3f5f7; border: 1px solid #d9dee3; border-radius: 4px; padding: 2px; }"
    "QPushButton#rsModeBtn { padding: 2px 12px; font-size: 11px; border: 1px solid transparent; border-radius: 3px; background: transparent; color: #5f6b7a; }"
    "QPushButton#rsModeBtn:checked { background: #ffffff; color: #208830; border: 1px solid #99c2a2; font-weight: 600; }"
  ) );

  connect( mGroup, QOverload<int>::of( &QButtonGroup::idClicked ),
           this, [this]( int id ) {
             const Mode newMode = static_cast<Mode>( id );
             if ( newMode == mMode )
               return;
             mMode = newMode;
             emit modeChanged( mMode );
           } );
}

void RsGeorefModeToggle::setMode( Mode m )
{
  if ( m == mMode )
    return;
  mMode = m;
  if ( auto *btn = mGroup->button( static_cast<int>( m ) ) )
  {
    QSignalBlocker blocker( mGroup );
    btn->setChecked( true );
  }
  emit modeChanged( mMode );
}
