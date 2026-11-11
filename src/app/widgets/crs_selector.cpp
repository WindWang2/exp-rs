// src/app/widgets/crs_selector.cpp — shared CRS input widget
#include "crs_selector.h"
#include "control_chrome.h"

#include <qgscoordinatereferencesystem.h>
#include <qgsprojectionselectiondialog.h>

#include <QLineEdit>
#include <QPushButton>

CrsSelector::CrsSelector( QWidget *parent )
  : QWidget( parent )
{
  m_edit = new QLineEdit( this );
  m_browseButton = new QPushButton( this );
  m_browseButton->setAutoDefault( false );
  const QString label = tr( "Choose the CRS from the projection selector." );
  m_browseButton->setToolTip( label );
  m_browseButton->setAccessibleName( label );
  SicnuUi::styleActionButton( *m_browseButton, SicnuUi::ActionTone::Secondary,
                              QIcon( QStringLiteral( ":/images/themes/default/mIconProjectionEnabled.svg" ) ) );
  setLayout( SicnuUi::makeFieldActionRow( m_edit, m_browseButton ) );

  connect( m_browseButton, &QPushButton::clicked, this, &CrsSelector::browse );
  connect( m_edit, &QLineEdit::textChanged, this, [this]( const QString & ) {
    emit crsChanged( crsString() );
  } );
}

QString CrsSelector::crsString() const
{
  return m_edit->text().trimmed();
}

void CrsSelector::setCrsString( const QString &crs )
{
  m_edit->setText( crs );
}

QgsCoordinateReferenceSystem CrsSelector::crs() const
{
  QgsCoordinateReferenceSystem crs;
  crs.createFromUserInput( crsString() );
  return crs;
}

bool CrsSelector::isValid() const
{
  return !crsString().isEmpty() && crs().isValid();
}

void CrsSelector::browse()
{
  QgsProjectionSelectionDialog dialog( this );
  if ( crs().isValid() )
    dialog.setCrs( crs() );
  if ( dialog.exec() == QDialog::Accepted )
  {
    const QgsCoordinateReferenceSystem selected = dialog.crs();
    if ( selected.isValid() )
      m_edit->setText( selected.authid().isEmpty() ? selected.toWkt() : selected.authid() );
  }
}
