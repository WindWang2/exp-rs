// rs_merge_classes_dialog.cpp — target-class chooser for merging sub-classes.
#include "rs_merge_classes_dialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

QMap<int, int> buildRecodeMap( const QList<int> &sourceIds, int targetId )
{
  QMap<int, int> map;
  for ( int id : sourceIds )
    map.insert( id, targetId );
  // The target id must map to itself so its pixels are preserved, not recoded.
  map.insert( targetId, targetId );
  return map;
}

RsMergeClassesDialog::RsMergeClassesDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "合并类别" ) );
  setObjectName( QStringLiteral( "rsMergeClassesDialog" ) );
  setModal( true );
  resize( 360, 200 );

  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 12, 12, 12, 12 );
  root->setSpacing( 10 );

  m_sourceLabel = new QLabel( this );
  m_sourceLabel->setWordWrap( true );
  root->addWidget( m_sourceLabel );

  auto *form = new QFormLayout;
  form->setSpacing( 8 );

  m_targetIdLabel = new QLabel( this );
  form->addRow( tr( "目标 ID:" ), m_targetIdLabel );

  m_nameEdit = new QLineEdit( this );
  form->addRow( tr( "目标名称:" ), m_nameEdit );

  m_colorBtn = new QPushButton( this );
  connect( m_colorBtn, &QPushButton::clicked, this, &RsMergeClassesDialog::pickColor );
  form->addRow( tr( "目标颜色:" ), m_colorBtn );

  root->addLayout( form );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
  root->addWidget( buttons );
}

void RsMergeClassesDialog::setSourceClassIds( const QList<int> &ids,
                                              const QString &firstName,
                                              const QColor &firstColor )
{
  m_sourceIds = ids;
  std::sort( m_sourceIds.begin(), m_sourceIds.end() );
  m_sourceIds.erase( std::unique( m_sourceIds.begin(), m_sourceIds.end() ),
                     m_sourceIds.end() );

  QStringList idTexts;
  idTexts.reserve( m_sourceIds.size() );
  for ( int id : m_sourceIds )
    idTexts.append( QString::number( id ) );
  m_sourceLabel->setText( tr( "合并以下类别: %1" ).arg( idTexts.join( QStringLiteral( ", " ) ) ) );

  if ( m_sourceIds.isEmpty() )
    return;

  m_targetIdLabel->setText( QString::number( m_sourceIds.first() )
                            + tr( " (自动)" ) );
  m_nameEdit->setText( firstName.isEmpty() ? QString::number( m_sourceIds.first() ) : firstName );
  m_color = firstColor.isValid() ? firstColor : QColor( QStringLiteral( "#888888" ) );
  refreshColorButton();
}

QString RsMergeClassesDialog::targetName() const
{
  return m_nameEdit->text().trimmed();
}

QColor RsMergeClassesDialog::targetColor() const
{
  return m_color;
}

int RsMergeClassesDialog::targetClassId() const
{
  return m_sourceIds.isEmpty() ? -1 : m_sourceIds.first();
}

void RsMergeClassesDialog::pickColor()
{
  const QColor c = QColorDialog::getColor( m_color, this, tr( "选择目标颜色" ) );
  if ( c.isValid() )
  {
    m_color = c;
    refreshColorButton();
  }
}

void RsMergeClassesDialog::refreshColorButton()
{
  m_colorBtn->setText( m_color.name() );
  // The stylesheet color name must be lowercase (#rrggbb) for setStyleSheet.
  m_colorBtn->setStyleSheet(
    QStringLiteral( "background-color: %1;" ).arg( m_color.name().toLower() ) );
}
