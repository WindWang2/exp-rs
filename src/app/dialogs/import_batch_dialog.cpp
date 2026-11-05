// src/app/dialogs/import_batch_dialog.cpp — see header for the contract.
#include "import_batch_dialog.h"

#include "data/data_manager.h"
#include "data/import_preflight.h"

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
    for ( const QJsonValue &value : items.toArray() )
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
    connect( m_importButton, &QPushButton::clicked, this, &QDialog::accept );
    connect( this, &QDialog::accepted, this, &ImportBatchDialog::runImport );
}

void ImportBatchDialog::runCheck()
{
    m_itemsView->clear();
    m_registeredPaths.clear();
    m_refusedPaths.clear();
    m_summaryLabel->setText( tr( "Checking %1 source(s)…" ).arg( m_sources.size() ) );

    sicnu::data::ImportSession::Options options;
    options.journalPath = m_journalPath;
    m_session = std::make_unique<sicnu::data::ImportSession>( m_manager, m_sources,
                                                              options );
    m_session->check();

    // Rows come from the ImportPreflight report (the same inspector the
    // session used): verdict + the first actionable finding + the dedup
    // prediction, so a refusal explains itself instead of reading "Failed".
    for ( const QString &source : m_sources )
    {
        const sicnu::data::ImportPreflightReport report =
          sicnu::data::ImportPreflight::inspect( source, m_manager );
        QString line = QStringLiteral( "[%1] %2" )
                         .arg( report.verdictString(), QFileInfo( source ).fileName() );
        line += QStringLiteral( "\n    %1" ).arg( QDir::toNativeSeparators( source ) );
        if ( report.alreadyRegistered )
            line += tr( "\n    Already registered in the catalog — importing again "
                        "will link to the existing asset." );
        QString fact;
        if ( report.resourceKind == sicnu::data::ImportPreflightResourceKind::Raster )
            fact = tr( "%1 · %2×%3 · %4 band(s)" )
                     .arg( report.driver )
                     .arg( report.width )
                     .arg( report.height )
                     .arg( report.bandCount );
        else if ( report.resourceKind == sicnu::data::ImportPreflightResourceKind::Vector )
            fact = tr( "vector · %1 layer(s)" ).arg( report.layerCount );
        if ( !fact.isEmpty() )
            line += QStringLiteral( "\n    %1" ).arg( fact );
        for ( const auto &finding : report.findings )
        {
            line += QStringLiteral( "\n    %1: %2" ).arg( finding.severity, finding.message );
            if ( !finding.remediation.isEmpty() )
                line += QStringLiteral( "\n        → %1" ).arg( finding.remediation );
            if ( finding.severity == QLatin1String( "error" ) )
                break; // the first blocking reason carries the row
        }
        auto *row = new QListWidgetItem( line, m_itemsView );
        row->setToolTip( source );
        m_itemsView->addItem( row );
    }

    refreshRowsFromSession( /*afterImport=*/false );
}

void ImportBatchDialog::runImport()
{
    if ( !m_session )
        return;
    // Exactly once: proceed() runs only from the checked state — the
    // accepted() signal and a direct call must not double-register.
    if ( m_session->state() != sicnu::data::ImportSessionState::ReadyToImport )
        return;
    m_session->proceed();
    refreshRowsFromSession( /*afterImport=*/true );
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
        if ( afterImport && index < m_itemsView->count() )
        {
            QString text = m_itemsView->item( index )->text();
            if ( item.state == sicnu::data::ImportSessionItemState::Pending )
                text.prepend( tr( "[not imported — batch interrupted] " ) );
            else
                text.prepend( QStringLiteral( "[%1] " ).arg( stateWord ) );
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
