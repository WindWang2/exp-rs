// src/app/dialogs/import_batch_dialog.cpp — see header for the contract.
#include "import_batch_dialog.h"

#include "data/data_manager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace sicnu::app
{

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
  , m_manager( manager )
  , m_sources( sources )
  , m_journalPath( journalPath.isEmpty() ? ImportBatchDialog::journalPath() : journalPath )
{
    setWindowTitle( tr( "Import Batch Review" ) );
    setModal( true );
    buildUi();
}

void ImportBatchDialog::buildUi()
{
    auto *layout = new QVBoxLayout( this );

    auto *heading = new QLabel(
      tr( "Preflight review — refused and needs-choice sources are listed with "
          "their reasons and will not be registered." ),
      this );
    heading->setWordWrap( true );
    layout->addWidget( heading );

    m_itemsView = new QListWidget( this );
    m_itemsView->setObjectName( QStringLiteral( "rsImportBatchItems" ) );
    m_itemsView->setAlternatingRowColors( true );
    m_itemsView->setWordWrap( true );
    layout->addWidget( m_itemsView, 1 );

    m_summaryLabel = new QLabel( this );
    m_summaryLabel->setObjectName( QStringLiteral( "rsImportBatchSummary" ) );
    m_summaryLabel->setWordWrap( true );
    layout->addWidget( m_summaryLabel );

    auto *buttons = new QHBoxLayout;
    buttons->addStretch( 1 );
    m_cancelButton = new QPushButton( tr( "Cancel" ), this );
    m_importButton = new QPushButton( tr( "Import Admissible Sources" ), this );
    m_importButton->setObjectName( QStringLiteral( "rsImportBatchImport" ) );
    m_importButton->setEnabled( false );
    buttons->addWidget( m_cancelButton );
    buttons->addWidget( m_importButton );
    layout->addLayout( buttons );

    connect( m_cancelButton, &QPushButton::clicked, this, &QDialog::reject );
    connect( m_importButton, &QPushButton::clicked, this, [this]() {
        if ( m_importFinished )
        {
            accept();
            return;
        }
        runImport();
        if ( !m_importFinished )
            return;
        // Keep the per-item import/refusal detail visible until the user
        // explicitly closes. `exec()` still returns Accepted on the second
        // click, so main_window_layers.cpp can consume registeredPaths().
        m_importButton->setText( tr( "Close" ) );
        m_cancelButton->setEnabled( false );
    } );
}

void ImportBatchDialog::runCheck()
{
    m_importFinished = false;
    m_importButton->setText( tr( "Import Admissible Sources" ) );
    m_cancelButton->setEnabled( true );
    m_itemsView->clear();
    m_registeredPaths.clear();
    m_refusedPaths.clear();
    m_summaryLabel->setText( tr( "Checking %1 source(s)…" ).arg( m_sources.size() ) );

    sicnu::data::ImportSession::Options options;
    options.journalPath = m_journalPath;
    m_session = std::make_unique<sicnu::data::ImportSession>( m_manager, m_sources,
                                                              options );
    // ONE preflight pass: the session inspects every source internally
    // (R1 P2-3 — the row loop used to re-inspect per source, a second
    // synchronous GDAL open per file on the GUI thread).
    m_session->check();

    // Base rows from the session's own per-item verdicts; the state words
    // are prepended by refreshRowsFromSession (single rendering path).
    const sicnu::data::ImportSessionSummary summary = m_session->summary();
    for ( const auto &item : summary.items )
    {
        auto *row = new QListWidgetItem( QDir::toNativeSeparators( item.path ),
                                         m_itemsView );
        row->setToolTip( item.path );
        m_itemsView->addItem( row );
    }

    // Publish the pending-state journal right after the review: an
    // abandoned batch is resumable from here (pendingPathsFromJournal),
    // using the session's own durable sidecar channel.
    m_session->saveJournal();
    refreshRowsFromSession( /*afterImport=*/false );
}

void ImportBatchDialog::runImport()
{
    if ( !m_session )
        return;
    // The session's own four-state contract (exactly once — the accepted()
    // signal and a direct call must not double-register):
    //   * ReadyToImport → proceed() (fresh check phase);
    //   * Failed/Cancelled → resume() — a journal-loaded session starts in
    //     these states and continues exactly its Pending items, re-verifying
    //     journaled-Registered ones against the catalog.
    const auto state = m_session->state();
    if ( state == sicnu::data::ImportSessionState::ReadyToImport )
        m_session->proceed();
    else if ( state == sicnu::data::ImportSessionState::Failed
              || state == sicnu::data::ImportSessionState::Cancelled )
        m_session->resume();
    else
        return;
    refreshRowsFromSession( /*afterImport=*/true );
    m_importFinished = true;
}

void ImportBatchDialog::refreshRowsFromSession( bool afterImport )
{
    if ( !m_session )
        return;
    const sicnu::data::ImportSessionSummary summary = m_session->summary();

    m_registeredPaths.clear();
    m_refusedPaths.clear();
    int reused = 0;
    int index = 0;
    for ( const auto &item : summary.items )
    {
        // Prefixes are rebuilt from the live state on every refresh; the
        // base text is always the item's own path.
        QString baseText = QDir::toNativeSeparators( item.path );
        QString stateWord;
        switch ( item.state )
        {
            case sicnu::data::ImportSessionItemState::Registered:
                stateWord = item.reusedExisting ? tr( "already registered" )
                                                : tr( "registered" );
                if ( item.reusedExisting )
                    ++reused;
                m_registeredPaths.append( item.path );
                break;
            case sicnu::data::ImportSessionItemState::Refused:
            case sicnu::data::ImportSessionItemState::Error:
                stateWord = item.state == sicnu::data::ImportSessionItemState::Refused
                              ? tr( "Refused" )
                              : tr( "Error" );
                m_refusedPaths.append( item.path );
                break;
            case sicnu::data::ImportSessionItemState::Skipped:
                stateWord = tr( "skipped (already in catalog)" );
                break;
            case sicnu::data::ImportSessionItemState::Pending:
                stateWord = tr( "importable" );
                break;
        }
        if ( index < m_itemsView->count() )
        {
            QString text = QStringLiteral( "[%1] %2" ).arg( stateWord, baseText );
            if ( item.state == sicnu::data::ImportSessionItemState::Pending && afterImport )
                text.prepend( tr( "[not imported — batch interrupted] " ) );
            if ( !item.message.isEmpty()
                 && ( item.state == sicnu::data::ImportSessionItemState::Refused
                      || item.state == sicnu::data::ImportSessionItemState::Error ) )
                text += QStringLiteral( "\n    %1" ).arg( item.message );
            m_itemsView->item( index )->setText( text );
        }
        ++index;
    }

    QString line = tr( "%1 importable · %2 refused · %3 registered · %4 reused" )
                     .arg( summary.pendingCount() )
                     .arg( summary.refusedCount() )
                     .arg( m_registeredPaths.size() - reused )
                     .arg( reused );
    if ( afterImport )
        line = tr( "Batch finished: %1 registered · %2 refused · %3 reused" )
                 .arg( m_registeredPaths.size() - reused )
                 .arg( m_refusedPaths.size() )
                 .arg( reused );
    if ( !summary.journalError.isEmpty() )
        line += tr( "\nJournal could not be persisted (%1) — this batch has no "
                    "durable resume point; the catalog remains the truth." )
                  .arg( summary.journalError );
    else if ( !summary.journalDurable )
        line += tr( "\nJournal is not durable in this session (in-process resume only)." );
    m_summaryLabel->setText( line );

    m_importButton->setEnabled( summary.pendingCount() > 0 );
}

} // namespace sicnu::app
