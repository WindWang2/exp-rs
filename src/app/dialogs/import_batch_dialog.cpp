// src/app/dialogs/import_batch_dialog.cpp — see header for the contract.
#include "import_batch_dialog.h"

#include "app/design_tokens.h"
#include "app/widgets/control_chrome.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace sicnu::app
{

using sicnu::data::ImportSessionItemState;
using sicnu::data::ImportSessionState;

QString ImportBatchDialog::journalPath()
{
    return QDir( QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) )
      .filePath( QStringLiteral( "import/import_session_journal.json" ) );
}

QStringList ImportBatchDialog::pendingPathsFromJournal( const QString &journalPath )
{
    QStringList pending;
    QFile file( journalPath );
    if ( !file.open( QIODevice::ReadOnly ) )
        return pending;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
    file.close();
    if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
        return pending;
    const QJsonValue items = doc.object().value( QStringLiteral( "items" ) );
    if ( !items.isArray() )
        return pending;
    for ( const auto &value : items.toArray() )
    {
        const QJsonObject item = value.toObject();
        if ( item.value( QStringLiteral( "state" ) ).toString()
             == QLatin1String( "pending" ) )
        {
            const QString path = item.value( QStringLiteral( "path" ) ).toString();
            if ( !path.isEmpty() )
                pending.append( path );
        }
    }
    return pending;
}

ImportBatchDialog::ImportBatchDialog( sicnu::data::DataManager *manager,
                                      const QStringList &sources, QWidget *parent,
                                      const QString &journalPath )
  : QDialog( parent )
  , m_session( manager, sources,
               { .journalPath = journalPath.isEmpty() ? ImportBatchDialog::journalPath() : journalPath,
                 .loadExistingJournal = false } )
{
    setWindowTitle( tr( "Import Batch Review" ) );
    setModal( true );
    buildUi( sources );
    resize( 720, 460 );
}

void ImportBatchDialog::buildUi( const QStringList &sources )
{
    using SicnuUi::Tokens::spacing;
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( spacing( 4 ), spacing( 4 ), spacing( 4 ), spacing( 4 ) );
    layout->setSpacing( spacing( 2 ) );

    m_heading = new QLabel( tr( "Checking sources" ), this );
    m_heading->setObjectName( QStringLiteral( "rsDialogSectionTitle" ) );
    layout->addWidget( m_heading );

    m_hint = new QLabel( this );
    m_hint->setObjectName( QStringLiteral( "rsDialogHint" ) );
    m_hint->setWordWrap( true );
    m_hint->hide();
    layout->addWidget( m_hint );

    m_progress = new QProgressBar( this );
    m_progress->setAccessibleName( tr( "Batch progress" ) );
    m_progress->setRange( 0, static_cast<int>( sources.size() ) );
    m_progress->setValue( 0 );
    m_progress->setFormat( QStringLiteral( "%v / %m" ) );
    layout->addWidget( m_progress );

    m_itemsView = new QListWidget( this );
    m_itemsView->setObjectName( QStringLiteral( "rsImportBatchItems" ) );
    m_itemsView->setAccessibleName( tr( "Sources and outcomes" ) );
    m_itemsView->setAlternatingRowColors( true );
    m_itemsView->setWordWrap( true );
    m_itemsView->setResizeMode( QListView::Adjust );
    for ( const QString &source : sources )
    {
        const QString name = QFileInfo( source ).fileName();
        auto *row = new QListWidgetItem( name.isEmpty() ? source : name, m_itemsView );
        row->setData( Qt::UserRole, QDir::toNativeSeparators( source ) );
    }
    layout->addWidget( m_itemsView, 1 );

    m_path = new QLineEdit( this );
    m_path->setReadOnly( true );
    m_path->setAccessibleName( tr( "Selected source path" ) );
    m_path->setPlaceholderText( tr( "Select a source to see its full path" ) );
    layout->addWidget( m_path );
    connect( m_itemsView, &QListWidget::currentItemChanged, this, [this]( QListWidgetItem *item ) {
        m_path->setText( item ? item->data( Qt::UserRole ).toString() : QString() );
        m_path->setCursorPosition( 0 );
    } );

    m_summaryLabel = new QLabel( this );
    m_summaryLabel->setObjectName( QStringLiteral( "rsImportBatchSummary" ) );
    m_summaryLabel->setTextFormat( Qt::PlainText );
    m_summaryLabel->setWordWrap( true );
    m_summaryLabel->hide();
    layout->addWidget( m_summaryLabel );

    auto *buttons = SicnuUi::makeControlRow();
    buttons->addStretch( 1 );
    m_cancelButton = new QPushButton( tr( "Cancel" ), this );
    m_cancelButton->setAutoDefault( false );
    SicnuUi::styleActionButton( *m_cancelButton, SicnuUi::ActionTone::Secondary );
    m_importButton = new QPushButton( tr( "Import Admissible Sources" ), this );
    m_importButton->setObjectName( QStringLiteral( "rsImportBatchImport" ) );
    SicnuUi::styleActionButton( *m_importButton, SicnuUi::ActionTone::Primary );
    m_importButton->setEnabled( false );
    buttons->addWidget( m_cancelButton );
    buttons->addWidget( m_importButton );
    layout->addLayout( buttons );

    connect( m_cancelButton, &QPushButton::clicked, this, &ImportBatchDialog::reject );
    connect( m_importButton, &QPushButton::clicked, this, &ImportBatchDialog::beginImport );

    m_stepTimer = new QTimer( this );
    connect( m_stepTimer, &QTimer::timeout, this, &ImportBatchDialog::advanceSession );
}

void ImportBatchDialog::showEvent( QShowEvent *event )
{
    QDialog::showEvent( event );
    if ( m_session.state() == ImportSessionState::Checking )
        m_stepTimer->start();
}

void ImportBatchDialog::beginImport()
{
    if ( m_resultsShown )
    {
        accept();
        return;
    }
    m_heading->setText( tr( "Importing sources" ) );
    m_hint->setText( tr( "Stop finishes the current source and keeps the items already imported." ) );
    m_cancelButton->setText( tr( "Stop" ) );
    m_importButton->setEnabled( false );
    m_progress->setRange( 0, m_session.summary().pendingCount() );
    m_progress->setValue( 0 );
    m_progress->show();
    m_summaryLabel->hide();
    m_stepTimer->start();
}

void ImportBatchDialog::advanceSession()
{
    // Process one source per event-loop turn on the DataManager's owning thread.
    const bool checking = m_session.state() == ImportSessionState::Checking;
    const int processed = checking ? m_session.checkSteps( 1 ) : m_session.proceedSteps( 1 );
    m_progress->setValue( m_progress->value() + processed );
    const auto state = m_session.state();
    if ( state == ImportSessionState::Checking || state == ImportSessionState::Importing )
        return;

    m_stepTimer->stop();
    if ( checking )
        m_session.saveJournal();
    showReview( state != ImportSessionState::ReadyToImport );
}

void ImportBatchDialog::reject()
{
    if ( m_resultsShown )
    {
        accept();
        return;
    }
    const bool importing = m_stepTimer->isActive() &&
                           m_session.state() != ImportSessionState::Checking;
    m_stepTimer->stop();
    m_session.cancel();
    m_session.saveJournal();
    if ( importing )
        showReview( true );
    else
        QDialog::reject();
}

void ImportBatchDialog::showReview( bool results )
{
    const auto summary = m_session.summary();
    m_registeredPaths.clear();
    m_refusedPaths.clear();
    m_displayableAssets.clear();
    m_resultsShown = results;
    m_progress->hide();
    m_hint->show();
    m_summaryLabel->show();

    int refused = 0;
    int reused = 0;
    int errors = 0;
    int index = 0;
    int firstIssue = -1;
    for ( const auto &item : summary.items )
    {
        QString stateWord;
        QString message = item.message;
        switch ( item.state )
        {
            case ImportSessionItemState::Registered:
                stateWord = item.reusedExisting ? tr( "already registered" ) : tr( "registered" );
                if ( item.reusedExisting )
                    ++reused;
                m_registeredPaths.append( item.path );
                break;
            case ImportSessionItemState::Refused:
                ++refused;
                m_refusedPaths.append( item.path );
                stateWord = tr( "Refused" );
                if ( item.verdict == QLatin1String( "needs_user_choice" ) )
                {
                    stateWord = tr( "Needs a selection" );
                    message = tr( "Choose a dataset or product type before importing this source." );
                }
                break;
            case ImportSessionItemState::Error:
                ++errors;
                m_refusedPaths.append( item.path );
                stateWord = tr( "Error" );
                break;
            case ImportSessionItemState::Skipped:
                ++reused;
                stateWord = tr( "already registered" );
                message.clear();
                break;
            case ImportSessionItemState::Pending:
                stateWord = results ? tr( "Pending" ) : tr( "importable" );
                break;
        }
        if ( item.state == ImportSessionItemState::Registered || item.state == ImportSessionItemState::Skipped )
        {
            if ( const auto assetId = sicnu::data::AssetId::fromString( item.assetId );
                 assetId && !m_displayableAssets.contains( *assetId ) )
                m_displayableAssets.append( *assetId );
        }
        if ( firstIssue < 0 && ( !message.isEmpty() || ( results && item.state == ImportSessionItemState::Pending ) ) )
            firstIssue = index;

        auto *row = m_itemsView->item( index++ );
        const QString name = QFileInfo( item.path ).fileName();
        QString text = QStringLiteral( "%1  ·  %2" ).arg( stateWord, name.isEmpty() ? item.path : name );
        if ( !message.isEmpty() )
            text += QStringLiteral( "\n%1" ).arg( message );
        row->setText( text );
    }
    if ( m_itemsView->count() > 0 )
        m_itemsView->setCurrentRow( qMax( 0, firstIssue ) );

    const int pending = summary.pendingCount();
    QString line;
    if ( results )
    {
        m_heading->setText( summary.state == ImportSessionState::Cancelled ? tr( "Batch stopped" )
                                                                           : tr( "Import results" ) );
        m_hint->setText( pending > 0 && summary.journalDurable
                          ? tr( "Pending sources can be offered again in the next batch." )
                          : tr( "Review the outcome for each source before closing." ) );
        line = tr( "%1 registered · %2 reused · %3 refused · %4 failed · %5 pending" )
                 .arg( summary.registeredCount() - reused ).arg( reused )
                 .arg( refused ).arg( errors ).arg( pending );
        m_importButton->setText( tr( "Close" ) );
        m_cancelButton->hide();
    }
    else
    {
        m_heading->setText( tr( "Review sources" ) );
        m_hint->setText( tr( "Check the source outcomes before starting the import." ) );
        line = tr( "%1 ready · %2 refused" ).arg( pending ).arg( refused );
    }
    if ( !summary.journalError.isEmpty() )
        line += tr( "\nCould not save the resume journal (%1)." ).arg( summary.journalError );
    else if ( !summary.journalDurable )
        line += tr( "\nThe resume journal is unavailable." );
    m_summaryLabel->setText( line );

    m_importButton->setEnabled( true );
    m_importButton->setDefault( true );
    if ( results )
        m_importButton->setFocus();
}

} // namespace sicnu::app
