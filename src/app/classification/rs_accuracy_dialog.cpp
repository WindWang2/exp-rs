// rs_accuracy_dialog.cpp — Phase 10A Task 10.9.

#include "rs_accuracy_dialog.h"

#include "core/sicnu_logging.h"
#include <QColor>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

namespace
{
QString fmt3( double v )
{
  return QString::number( v, 'f', 3 );
}
} // namespace

RsAccuracyDialog::RsAccuracyDialog( const RsAccuracyAssessment::Result &result,
                                    const QHash<int, QString> &classNames,
                                    QWidget *parent )
  : QDialog( parent )
  , mResult( result )
  , mNames( classNames )
{
  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Accuracy assessment: OA=%1%, Kappa=%2, classes=%3" )
    .arg( mResult.overallAccuracy * 100.0, 0, 'f', 1 )
    .arg( mResult.kappa, 0, 'f', 3 )
    .arg( mResult.classIds.size() ) );
  setWindowTitle( tr( "Accuracy Assessment" ) );
  resize( 720, 560 );

  auto *layout = new QVBoxLayout( this );

  // --- Header ------------------------------------------------------------
  auto *header = new QLabel( this );
  header->setText( tr( "Overall Accuracy: %1%   Kappa: %2" )
                     .arg( mResult.overallAccuracy * 100.0, 0, 'f', 1 )
                     .arg( mResult.kappa, 0, 'f', 3 ) );
  QFont hf = header->font();
  hf.setPointSize( 18 );
  hf.setBold( true );
  header->setFont( hf );
  layout->addWidget( header );

  // --- Confusion matrix --------------------------------------------------
  layout->addWidget( new QLabel( tr( "Confusion Matrix (rows = True, cols = Predicted)" ), this ) );

  const int n = mResult.classIds.size();
  auto *cm = new QTableWidget( n, n, this );
  QStringList headers;
  headers.reserve( n );
  for ( int id : mResult.classIds )
    headers << classLabel( id );
  cm->setHorizontalHeaderLabels( headers );
  cm->setVerticalHeaderLabels( headers );
  cm->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
  cm->verticalHeader()->setSectionResizeMode( QHeaderView::Stretch );

  for ( int rIdx = 0; rIdx < n; ++rIdx )
  {
    for ( int cIdx = 0; cIdx < n; ++cIdx )
    {
      const int v = mResult.confusion.at<int>( rIdx, cIdx );
      auto *item = new QTableWidgetItem( QString::number( v ) );
      item->setTextAlignment( Qt::AlignCenter );
      if ( rIdx == cIdx )
      {
        item->setBackground( QColor( "#208830" ) );
        item->setForeground( Qt::white );
      }
      else if ( v >= 10 )
      {
        item->setForeground( QColor( "#cf222e" ) );
      }
      cm->setItem( rIdx, cIdx, item );
    }
  }
  layout->addWidget( cm, 2 );

  // --- Per-class metrics -------------------------------------------------
  layout->addWidget( new QLabel( tr( "Per-class Metrics" ), this ) );

  auto *pm = new QTableWidget( n, 4, this );
  pm->setHorizontalHeaderLabels(
    QStringList{ tr( "Class" ), tr( "Producer Acc." ),
                 tr( "User Acc." ), tr( "F1" ) } );
  pm->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
  pm->verticalHeader()->setVisible( false );
  for ( int i = 0; i < n; ++i )
  {
    const int id = mResult.classIds[i];
    pm->setItem( i, 0, new QTableWidgetItem( classLabel( id ) ) );
    pm->setItem( i, 1, new QTableWidgetItem( fmt3( mResult.producerAcc.value( id ) ) ) );
    pm->setItem( i, 2, new QTableWidgetItem( fmt3( mResult.userAcc.value( id ) ) ) );
    pm->setItem( i, 3, new QTableWidgetItem( fmt3( mResult.f1.value( id ) ) ) );
    for ( int c = 1; c < 4; ++c )
      pm->item( i, c )->setTextAlignment( Qt::AlignCenter );
  }
  layout->addWidget( pm, 1 );

  // --- Buttons -----------------------------------------------------------
  auto *bb = new QDialogButtonBox( QDialogButtonBox::Close, this );
  auto *exportBtn = bb->addButton( tr( "Export CSV..." ),
                                   QDialogButtonBox::ActionRole );
  connect( exportBtn, &QPushButton::clicked, this, &RsAccuracyDialog::exportCsv );
  connect( bb, &QDialogButtonBox::rejected, this, &QDialog::reject );
  layout->addWidget( bb );
}

QString RsAccuracyDialog::classLabel( int id ) const
{
  if ( mNames.contains( id ) && !mNames.value( id ).isEmpty() )
    return mNames.value( id );
  return QStringLiteral( "Class %1" ).arg( id );
}

void RsAccuracyDialog::exportCsv()
{
  const QString path = QFileDialog::getSaveFileName(
    this, tr( "Export accuracy report (CSV)" ), QString(),
    tr( "CSV files (*.csv)" ) );
  if ( path.isEmpty() )
    return;

  QFile f( path );
  if ( !f.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QMessageBox::warning( this, tr( "Export failed" ),
                          tr( "Cannot open file for writing: %1" ).arg( path ) );
    return;
  }
  QTextStream out( &f );

  out << "Overall Accuracy," << mResult.overallAccuracy << "\n";
  out << "Kappa," << mResult.kappa << "\n\n";

  out << "Confusion Matrix\n";
  out << ",";
  for ( int id : mResult.classIds )
    out << "Pred " << classLabel( id ) << ",";
  out << "\n";
  for ( int rIdx = 0; rIdx < mResult.classIds.size(); ++rIdx )
  {
    out << "True " << classLabel( mResult.classIds[rIdx] ) << ",";
    for ( int cIdx = 0; cIdx < mResult.classIds.size(); ++cIdx )
      out << mResult.confusion.at<int>( rIdx, cIdx ) << ",";
    out << "\n";
  }
  out << "\nPer-class Metrics\n";
  out << "Class,ProducerAcc,UserAcc,F1\n";
  for ( int id : mResult.classIds )
  {
    out << classLabel( id ) << ","
        << mResult.producerAcc.value( id ) << ","
        << mResult.userAcc.value( id ) << ","
        << mResult.f1.value( id ) << "\n";
  }
  f.close();
  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Accuracy report exported: %1" ).arg( path ) );
}
