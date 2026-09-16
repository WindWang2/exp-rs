// src/app/widgets/band_composite_palette.cpp — D13 RGB composite selector
#include "band_composite_palette.h"

#include <raster/qgsrasterlayer.h>

#include <QComboBox>
#include <QLabel>

#include <algorithm>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>

namespace exp_gui
{
  namespace
  {
    QString bandLabel( int band1Based )
    {
      return band1Based == 0 ? QObject::tr( "— unset —" ) : QObject::tr( "Band %1" ).arg( band1Based );
    }
  } // namespace

  BandCompositePalette::BandCompositePalette( QWidget *parent )
    : QWidget( parent )
  {
    m_redCombo = new QComboBox( this );
    m_greenCombo = new QComboBox( this );
    m_blueCombo = new QComboBox( this );
    m_stretchCombo = new QComboBox( this );
    m_clipPercent = new QDoubleSpinBox( this );
    m_clipPercent->setRange( 0.0, 50.0 );
    m_clipPercent->setSuffix( QStringLiteral( "%" ) );
    m_clipPercent->setValue( 2.0 );
    m_stretchCombo->addItem( tr( "No stretch" ), 0 );
    m_stretchCombo->addItem( tr( "Linear min/max" ), 1 );
    m_stretchCombo->addItem( tr( "Min/max clip" ), 2 );
    m_stretchCombo->setCurrentIndex( 1 );

    auto comboRow = []( const QString &labelText, QComboBox *combo ) {
      auto *row = new QWidget;
      auto *layout = new QHBoxLayout( row );
      layout->setContentsMargins( 0, 0, 0, 0 );
      layout->addWidget( new QLabel( labelText, row ) );
      layout->addWidget( combo, 1 );
      return row;
    };

    auto *form = new QFormLayout( this );
    form->addRow( comboRow( tr( "Red" ), m_redCombo ), comboRow( tr( "Green" ), m_greenCombo ) );
    form->addRow( comboRow( tr( "Blue" ), m_blueCombo ), m_stretchCombo );
    form->addRow( tr( "Clip" ), m_clipPercent );

    for ( QComboBox *combo : { m_redCombo, m_greenCombo, m_blueCombo } )
    {
      connect( combo, &QComboBox::activated, this, [this] { applyComboMapping(); } );
    }
    connect( m_stretchCombo, &QComboBox::activated, this, [this] {
      emit stretchMethodChanged( m_stretchCombo->currentData().toInt(), m_clipPercent->value() );
    } );
    connect( m_clipPercent, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]( double value ) {
      emit stretchMethodChanged( m_stretchCombo->currentData().toInt(), value );
    } );
  }

  BandCompositePalette::~BandCompositePalette() = default;

  int BandCompositePalette::clampBand( int band1Based ) const
  {
    if ( m_bandCount <= 0 )
      return 0;
    return std::clamp( band1Based, 0, m_bandCount );
  }

  void BandCompositePalette::bindRasterLayer( QgsRasterLayer *layer )
  {
    m_layer = layer;
    m_bandCount = layer ? layer->bandCount() : 0;
    m_redBand = m_greenBand = m_blueBand = 0;

    for ( QComboBox *combo : { m_redCombo, m_greenCombo, m_blueCombo } )
    {
      QSignalBlocker blocker( combo );
      combo->clear();
      combo->addItem( bandLabel( 0 ), 0 );
      for ( int b = 1; b <= m_bandCount; ++b )
        combo->addItem( bandLabel( b ), b );
      combo->setCurrentIndex( 0 );
    }
  }

  void BandCompositePalette::setRgbMapping( int redBand1Based, int greenBand1Based, int blueBand1Based )
  {
    if ( m_layer.isNull() || m_bandCount <= 0 )
      return; // nothing bound (or the bound layer died): refuse silently-but-safely

    m_redBand = clampBand( redBand1Based );
    m_greenBand = clampBand( greenBand1Based );
    m_blueBand = clampBand( blueBand1Based );

    const QSignalBlocker r( m_redCombo );
    const QSignalBlocker g( m_greenCombo );
    const QSignalBlocker b( m_blueCombo );
    m_redCombo->setCurrentIndex( m_redCombo->findData( m_redBand ) );
    m_greenCombo->setCurrentIndex( m_greenCombo->findData( m_greenBand ) );
    m_blueCombo->setCurrentIndex( m_blueCombo->findData( m_blueBand ) );

    emit bandMappingChanged( m_redBand, m_greenBand, m_blueBand );
  }

  void BandCompositePalette::applyComboMapping()
  {
    m_redBand = m_redCombo->currentData().toInt();
    m_greenBand = m_greenCombo->currentData().toInt();
    m_blueBand = m_blueCombo->currentData().toInt();
    emit bandMappingChanged( m_redBand, m_greenBand, m_blueBand );
  }

  void BandCompositePalette::setStretchMethod( int stretchMode, double clipPercent )
  {
    const int mode = std::clamp( stretchMode, 0, 2 );
    const double clip = std::clamp( clipPercent, 0.0, 50.0 );
    const QSignalBlocker s( m_stretchCombo );
    const QSignalBlocker c( m_clipPercent );
    const int index = m_stretchCombo->findData( mode );
    if ( index >= 0 )
      m_stretchCombo->setCurrentIndex( index );
    m_clipPercent->setValue( clip );
    emit stretchMethodChanged( mode, clip );
  }
} // namespace exp_gui
