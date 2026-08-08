// src/app/widgets/band_role_combo.cpp — shared semantic band-role selector
#include "band_role_combo.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDebug>
#include <QString>

BandRoleCombo::BandRoleCombo( QWidget *parent )
  : QComboBox( parent )
{
}

void BandRoleCombo::setRaster( const QString &source )
{
  clear();
  m_hasRaster = false;

  addItem( tr( "自动（按产品语义角色）" ), 0 );

  GdalDatasetWrapper ds;
  if ( !ds.open( source ) )
  {
    // Do not silently degrade to "auto": surface the open failure in the log
    // so a mis-typed path / unreadable provider is diagnosable (ADR 0105).
    const QString lastError = ds.lastError();
    if ( !lastError.isEmpty() )
      qWarning() << "BandRoleCombo: cannot open raster for band roles:" << lastError;
    else
      qWarning() << "BandRoleCombo: cannot open raster:" << source;
    return;
  }

  const int bandCount = ds.bandCount();
  if ( bandCount <= 0 )
    return;

  for ( int b = 1; b <= bandCount; ++b )
  {
    QString label = tr( "波段 %1" ).arg( b );
    const sicnu::data::BandRole role = sicnu::data::bandRoleFromString(
      ds.bandMetadataItem( b, "SICNU_BAND_ROLE" ) );
    if ( role != sicnu::data::BandRole::Unknown )
    {
      const QString roleName = sicnu::data::bandRoleDisplayName( role );
      if ( !roleName.isEmpty() )
        label = tr( "波段 %1 (%2)" ).arg( b ).arg( roleName );
    }
    addItem( label, b );
    setItemData( count() - 1, static_cast<int>( role ), Qt::UserRole + 1 );
  }
  m_hasRaster = true;
}

int BandRoleCombo::selectedBand() const
{
  if ( !m_hasRaster )
    return 0;
  const int band = currentData().toInt();
  return band > 0 ? band : 0;
}

sicnu::data::BandRole BandRoleCombo::selectedRole() const
{
  const int band = selectedBand();
  if ( band <= 0 )
    return sicnu::data::BandRole::Unknown;

  // Role of the selected band, recomputed from its item's label is fragile;
  // keep the role index alongside the band number in the item data.
  const QVariant roleVariant = itemData( currentIndex(), Qt::UserRole + 1 );
  return roleVariant.isValid()
    ? static_cast<sicnu::data::BandRole>( roleVariant.toInt() )
    : sicnu::data::BandRole::Unknown;
}

void BandRoleCombo::selectBandByRole( sicnu::data::BandRole role )
{
  if ( !m_hasRaster )
    return;
  for ( int i = 0; i < count(); ++i )
  {
    const int band = itemData( i ).toInt();
    const QVariant roleVariant = itemData( i, Qt::UserRole + 1 );
    if ( band > 0 && roleVariant.isValid()
         && static_cast<sicnu::data::BandRole>( roleVariant.toInt() ) == role )
    {
      setCurrentIndex( i );
      return;
    }
  }
  setCurrentIndex( 0 ); // auto
}
