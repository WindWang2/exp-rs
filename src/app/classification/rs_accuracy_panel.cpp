// rs_accuracy_panel.cpp — classification workflow Step 5 accuracy embed.

#include "rs_accuracy_panel.h"

#include "core/sicnu_logging.h"
#include "dialogs/dialog_help_catalog.h"
#include "durable_file_write.h"

#include <QColor>
#include <QFile>
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

// #1563: class names are free-form user input (rs_class_table_widget.cpp
// writes them straight from the table cell into RsClassDef), so a CSV field
// built from one can carry a comma, a double quote or a newline and silently
// shift the report's columns. Quote per RFC 4180 (doubling embedded quotes);
// a leading spreadsheet-formula trigger (=, +, -, @) is prefixed with an
// apostrophe so a name like "=cmd|' /C calc'!A0" is not evaluated on open.
QString csvField( const QString &value )
{
  QString text = value;
  if ( text.startsWith( QLatin1Char( '=' ) ) || text.startsWith( QLatin1Char( '+' ) )
       || text.startsWith( QLatin1Char( '-' ) ) || text.startsWith( QLatin1Char( '@' ) ) )
    text.prepend( QLatin1Char( '\'' ) );
  if ( text.contains( QLatin1Char( ',' ) ) || text.contains( QLatin1Char( '"' ) )
       || text.contains( QLatin1Char( '\n' ) ) || text.contains( QLatin1Char( '\r' ) ) )
  {
    text.replace( QLatin1String( "\"" ), QLatin1String( "\"\"" ) );
    text.prepend( QLatin1Char( '"' ) );
    text.append( QLatin1Char( '"' ) );
  }
  return text;
}
} // namespace

RsAccuracyPanel::RsAccuracyPanel( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsAccuracyPanel" ) );

  auto *layout = new QVBoxLayout( this );
  layout->setContentsMargins( 0, 0, 0, 0 );
  layout->setSpacing( 6 );

  SicnuDialogHelp::tip( this, SicnuDialogHelp::shortForTool(
                         QStringLiteral( "accuracy" ), tr( "Accuracy Assessment" ) ) );

  mEmptyHint = new QLabel(
    tr( "After full-image classification (with validation / holdout accuracy), shows OA, Kappa and the confusion matrix here." ),
    this );
  mEmptyHint->setObjectName( QStringLiteral( "rsAccuracyEmptyHint" ) );
  mEmptyHint->setWordWrap( true );
  mEmptyHint->setObjectName( QStringLiteral( "rsEmptyHint" ) );
  SicnuDialogHelp::tip( mEmptyHint, tr(
    "Overall accuracy OA, Kappa, confusion matrix (rows = truth, columns = prediction), producer's / user's accuracy and F1.")  );
  layout->addWidget( mEmptyHint );

  mHeaderLabel = new QLabel( this );
  mHeaderLabel->setObjectName( QStringLiteral( "rsAccuracyHeader" ) );
  QFont hf = mHeaderLabel->font();
  hf.setPointSize( 16 );
  hf.setBold( true );
  mHeaderLabel->setFont( hf );
  mHeaderLabel->setVisible( false );
  layout->addWidget( mHeaderLabel );

  auto *cmTitle = new QLabel( tr( "Confusion matrix (rows = truth, columns = prediction)" ), this );
  cmTitle->setObjectName( QStringLiteral( "rsAccuracyCmTitle" ) );
  layout->addWidget( cmTitle );

  mConfusion = new QTableWidget( this );
  mConfusion->setObjectName( QStringLiteral( "rsAccuracyConfusion" ) );
  mConfusion->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
  mConfusion->verticalHeader()->setSectionResizeMode( QHeaderView::Stretch );
  mConfusion->setMinimumHeight( 120 );
  SicnuDialogHelp::tip( mConfusion, tr(
    "Confusion matrix: rows = true classes, columns = predicted classes; the diagonal holds correctly classified sample counts.")  );
  layout->addWidget( mConfusion, 2 );

  auto *pmTitle = new QLabel( tr( "Per-Class Metrics" ), this );
  pmTitle->setObjectName( QStringLiteral( "rsAccuracyPmTitle" ) );
  layout->addWidget( pmTitle );

  mPerClass = new QTableWidget( this );
  mPerClass->setObjectName( QStringLiteral( "rsAccuracyPerClass" ) );
  mPerClass->setHorizontalHeaderLabels(
    QStringList{ tr( "Class" ), tr( "Producer's Accuracy" ),
                 tr( "User's Accuracy" ), tr( "F1" ) } );
  mPerClass->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
  mPerClass->verticalHeader()->setVisible( false );
  mPerClass->setMinimumHeight( 80 );
  SicnuDialogHelp::tip( mPerClass, tr(
    "Producer's accuracy ≈ recall; user's accuracy ≈ precision; F1 is their harmonic mean.")  );
  layout->addWidget( mPerClass, 1 );

  mExportBtn = new QPushButton( tr( "Export CSV..." ), this );
  mExportBtn->setObjectName( QStringLiteral( "rsAccuracyExportCsv" ) );
  mExportBtn->setEnabled( false );
  SicnuDialogHelp::tip( mExportBtn, tr( "Exports the accuracy table as a CSV report." ) );
  connect( mExportBtn, &QPushButton::clicked, this, &RsAccuracyPanel::exportCsv );
  layout->addWidget( mExportBtn );
}

void RsAccuracyPanel::setResult( const RsAccuracyAssessment::Result &r,
                                 const QHash<int, QString> &classNames )
{
  mResult = r;
  mNames = classNames;
  mHasResult = !r.classIds.isEmpty();
  rebuildTables();

  if ( mHasResult )
  {
    SICNU_LOG_INFO( SicnuLogTags::Classification,
                    QString( "Accuracy panel: OA=%1%, Kappa=%2, classes=%3" )
                      .arg( mResult.overallAccuracy * 100.0, 0, 'f', 1 )
                      .arg( mResult.kappa, 0, 'f', 3 )
                      .arg( mResult.classIds.size() ) );
  }
}

void RsAccuracyPanel::clear()
{
  mResult = RsAccuracyAssessment::Result{};
  mNames.clear();
  mHasResult = false;
  rebuildTables();
}

bool RsAccuracyPanel::hasResult() const
{
  return mHasResult;
}

QString RsAccuracyPanel::classLabel( int id ) const
{
  if ( mNames.contains( id ) && !mNames.value( id ).isEmpty() )
    return mNames.value( id );
  return QStringLiteral( "Class %1" ).arg( id );
}

void RsAccuracyPanel::rebuildTables()
{
  if ( !mHasResult )
  {
    if ( mEmptyHint )
      mEmptyHint->setVisible( true );
    if ( mHeaderLabel )
    {
      mHeaderLabel->clear();
      mHeaderLabel->setVisible( false );
    }
    if ( mConfusion )
    {
      mConfusion->clear();
      mConfusion->setRowCount( 0 );
      mConfusion->setColumnCount( 0 );
    }
    if ( mPerClass )
    {
      mPerClass->setRowCount( 0 );
    }
    if ( mExportBtn )
      mExportBtn->setEnabled( false );
    return;
  }

  if ( mEmptyHint )
    mEmptyHint->setVisible( false );

  if ( mHeaderLabel )
  {
    mHeaderLabel->setText(
      tr( "Overall accuracy: %1%   Kappa: %2" )
        .arg( mResult.overallAccuracy * 100.0, 0, 'f', 1 )
        .arg( mResult.kappa, 0, 'f', 3 ) );
    mHeaderLabel->setVisible( true );
  }

  const int n = mResult.classIds.size();
  QStringList headers;
  headers.reserve( n );
  for ( int id : mResult.classIds )
    headers << classLabel( id );

  if ( mConfusion )
  {
    mConfusion->clear();
    mConfusion->setRowCount( n );
    mConfusion->setColumnCount( n );
    mConfusion->setHorizontalHeaderLabels( headers );
    mConfusion->setVerticalHeaderLabels( headers );

    for ( int rIdx = 0; rIdx < n; ++rIdx )
    {
      for ( int cIdx = 0; cIdx < n; ++cIdx )
      {
        const int v = ( !mResult.confusion.empty()
                        && rIdx < mResult.confusion.rows
                        && cIdx < mResult.confusion.cols )
                        ? mResult.confusion.at<int>( rIdx, cIdx )
                        : 0;
        auto *item = new QTableWidgetItem( QString::number( v ) );
        item->setTextAlignment( Qt::AlignCenter );
        item->setFlags( item->flags() & ~Qt::ItemIsEditable );
        if ( rIdx == cIdx )
        {
          item->setBackground( QColor( QStringLiteral( "#208830" ) ) );
          item->setForeground( Qt::white );
        }
        else if ( v >= 10 )
        {
          item->setForeground( QColor( QStringLiteral( "#cf222e" ) ) );
        }
        mConfusion->setItem( rIdx, cIdx, item );
      }
    }
  }

  if ( mPerClass )
  {
    mPerClass->setRowCount( n );
    for ( int i = 0; i < n; ++i )
    {
      const int id = mResult.classIds[i];
      auto *nameItem = new QTableWidgetItem( classLabel( id ) );
      nameItem->setFlags( nameItem->flags() & ~Qt::ItemIsEditable );
      mPerClass->setItem( i, 0, nameItem );

      auto *pItem = new QTableWidgetItem( fmt3( mResult.producerAcc.value( id ) ) );
      auto *uItem = new QTableWidgetItem( fmt3( mResult.userAcc.value( id ) ) );
      auto *fItem = new QTableWidgetItem( fmt3( mResult.f1.value( id ) ) );
      pItem->setTextAlignment( Qt::AlignCenter );
      uItem->setTextAlignment( Qt::AlignCenter );
      fItem->setTextAlignment( Qt::AlignCenter );
      pItem->setFlags( pItem->flags() & ~Qt::ItemIsEditable );
      uItem->setFlags( uItem->flags() & ~Qt::ItemIsEditable );
      fItem->setFlags( fItem->flags() & ~Qt::ItemIsEditable );
      mPerClass->setItem( i, 1, pItem );
      mPerClass->setItem( i, 2, uItem );
      mPerClass->setItem( i, 3, fItem );
    }
  }

  if ( mExportBtn )
    mExportBtn->setEnabled( true );
}

bool RsAccuracyPanel::exportCsv()
{
  if ( !mHasResult )
    return false;

  const QString path = QFileDialog::getSaveFileName(
    this, tr( "Export Accuracy Report (CSV)" ), QString(),
    tr( "CSV files (*.csv)" ) );
  if ( path.isEmpty() )
    return false;

  // APP-10 (R14-D WP-B): serialize then publish through the shared
  // crash-or-valid authority (temp + fsync + atomic rename); the success log
  // line is emitted only when the bytes actually landed.
  QByteArray bytes;
  QTextStream out( &bytes, QIODevice::WriteOnly );

  out << "Overall Accuracy," << mResult.overallAccuracy << "\n";
  out << "Kappa," << mResult.kappa << "\n\n";

  out << "Confusion Matrix\n";
  out << ",";
  for ( int id : mResult.classIds )
    out << csvField( QStringLiteral( "Pred %1" ).arg( classLabel( id ) ) ) << ",";
  out << "\n";
  for ( int rIdx = 0; rIdx < mResult.classIds.size(); ++rIdx )
  {
    out << csvField( QStringLiteral( "True %1" ).arg( classLabel( mResult.classIds[rIdx] ) ) ) << ",";
    for ( int cIdx = 0; cIdx < mResult.classIds.size(); ++cIdx )
    {
      const int v = ( !mResult.confusion.empty()
                      && rIdx < mResult.confusion.rows
                      && cIdx < mResult.confusion.cols )
                      ? mResult.confusion.at<int>( rIdx, cIdx )
                      : 0;
      out << v << ",";
    }
    out << "\n";
  }
  out << "\nPer-class Metrics\n";
  out << "Class,ProducerAcc,UserAcc,F1\n";
  for ( int id : mResult.classIds )
  {
    out << csvField( classLabel( id ) ) << ","
        << mResult.producerAcc.value( id ) << ","
        << mResult.userAcc.value( id ) << ","
        << mResult.f1.value( id ) << "\n";
  }
  out.flush();
#if defined( Q_OS_WIN )
  // The old lane opened the file with QIODevice::Text (LF->CRLF on Windows);
  // keep the published bytes byte-identical to what it produced.
  bytes.replace( '\n', "\r\n" );
#endif
  const sicnu::DurableWriteResult result = sicnu::writeBytesDurably( path, bytes );
  if ( !result.ok )
  {
    QMessageBox::warning( this, tr( "Export Failed" ),
                          tr( "Cannot write file: %1\n%2" ).arg( path, result.error ) );
    return false;
  }
  SICNU_LOG_INFO( SicnuLogTags::Classification,
                  QString( "Accuracy report exported: %1" ).arg( path ) );
  return true;
}
