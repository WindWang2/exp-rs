// src/app/widgets/crs_selector.cpp — shared CRS input widget
#include "crs_selector.h"

#include <qgscoordinatereferencesystem.h>
#include <qgsprojectionselectiondialog.h>

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

CrsSelector::CrsSelector( QWidget *parent )
  : QWidget( parent )
{
  auto *layout = new QHBoxLayout( this );
  layout->setContentsMargins( 0, 0, 0, 0 );
  layout->setSpacing( 6 );

  m_edit = new QLineEdit( this );
  layout->addWidget( m_edit, 1 );

  m_browseButton = new QPushButton( tr( "…" ), this );
  m_browseButton->setToolTip( tr( "从投影选择器选择 CRS。" ) );
  layout->addWidget( m_browseButton );

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
