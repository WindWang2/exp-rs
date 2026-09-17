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
    tr( "RPC Physical Model" )
  };
  const QStringList tips = {
    tr( "Image-to-map registration: pick points on the image and enter geographic coordinates." ),
    tr( "Image-to-image registration: pick conjugate points on the image to correct and the reference image." ),
    tr( "RPC physical model: correction using rational polynomial coefficients." )
  };

  for ( int i = 0; i < labels.size(); ++i )
  {
    auto *btn = new QPushButton( labels[i], this );
    btn->setToolTip( tips[i] );
    btn->setStatusTip( tips[i] );
    btn->setCheckable( true );
    btn->setObjectName( QStringLiteral( "rsModeBtn" ) );
    btn->setProperty( "modeIndex", i );
    if ( i == 0 )
      btn->setChecked( true );
    mGroup->addButton( btn, i );
    lay->addWidget( btn );
  }

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
