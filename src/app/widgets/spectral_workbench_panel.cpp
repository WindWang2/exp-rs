// spectral_workbench_panel.cpp — Spectral Workbench 11 endmember/library panel
#include "spectral_workbench_panel.h"

#include "processing/algorithms/endmember_analysis.h"
#include "processing/algorithms/spectral_table.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    // Keep the matrix text render bounded; curated endmember sets are small.
    constexpr int kMaxMatrixRows = 64;
}

SpectralWorkbenchPanel::SpectralWorkbenchPanel( QWidget *parent )
    : QWidget( parent )
{
    auto *layout = new QVBoxLayout( this );

    auto *pathRow = new QHBoxLayout;
    m_pathEdit = new QLineEdit( this );
    m_pathEdit->setPlaceholderText( tr( "exp-rs:spectral-table artifact path" ) );
    m_pathEdit->setObjectName( QStringLiteral( "spectralWorkbenchPathEdit" ) );
    m_loadButton = new QPushButton( tr( "Load table" ), this );
    m_loadButton->setObjectName( QStringLiteral( "spectralWorkbenchLoadButton" ) );
    pathRow->addWidget( m_pathEdit, 1 );
    pathRow->addWidget( m_loadButton );
    layout->addLayout( pathRow );

    m_statusLabel = new QLabel( this );
    m_statusLabel->setObjectName( QStringLiteral( "spectralWorkbenchStatus" ) );
    layout->addWidget( m_statusLabel );

    auto *splitter = new QSplitter( Qt::Vertical, this );

    m_spectrumList = new QListWidget( splitter );
    m_spectrumList->setObjectName( QStringLiteral( "spectralWorkbenchSpectraList" ) );

    m_matrixView = new QPlainTextEdit( splitter );
    m_matrixView->setObjectName( QStringLiteral( "spectralWorkbenchMatrixView" ) );
    m_matrixView->setReadOnly( true );
    m_matrixView->setPlaceholderText( tr( "Pairwise SAM angle matrix (radians)" ) );

    m_detailsView = new QPlainTextEdit( splitter );
    m_detailsView->setObjectName( QStringLiteral( "spectralWorkbenchDetailsView" ) );
    m_detailsView->setReadOnly( true );
    m_detailsView->setPlaceholderText( tr( "Selected spectrum details" ) );

    splitter->addWidget( m_spectrumList );
    splitter->addWidget( m_matrixView );
    splitter->addWidget( m_detailsView );
    splitter->setStretchFactor( 0, 2 );
    splitter->setStretchFactor( 1, 3 );
    splitter->setStretchFactor( 2, 2 );
    layout->addWidget( splitter, 1 );

    connect( m_loadButton, &QPushButton::clicked, this, [this]
    {
        QString error;
        if ( !setTablePath( m_pathEdit->text(), &error ) )
            m_statusLabel->setText( error.isEmpty()
                                        ? tr( "Failed to load table" )
                                        : error );
    } );
    connect( m_spectrumList, &QListWidget::currentRowChanged, this,
             [this]( int row ) { onRowActivated( row ); } );
}

bool SpectralWorkbenchPanel::setTablePath( const QString &path, QString *errorMessage )
{
    SpectralTable::Table table;
    QString error;
    if ( !SpectralTable::loadValidated( path, &table, &error ) )
    {
        if ( errorMessage )
            *errorMessage = error;
        return false;
    }

    m_tablePath = path;
    m_count = table.count();
    m_bandCount = table.bandCount;
    m_spectra = table.spectra;
    m_labels = table.labels;
    while ( m_labels.size() < m_count )
        m_labels.append( QStringLiteral( "spectrum_%1" ).arg( m_labels.size() + 1 ) );

    refreshViews();

    // Digest/provenance line: artifact identity stays visible in the panel.
    QString provenance = tr( "%1 rows x %2 bands — digest %3…" )
                             .arg( m_count )
                             .arg( m_bandCount )
                             .arg( table.digestHex.left( 12 ) );
    if ( !table.license.isEmpty() )
        provenance += tr( " — license: %1" ).arg( table.license );
    if ( table.provenance.derived && !table.provenance.sourceOperator.isEmpty() )
        provenance += tr( " — via %1" ).arg( table.provenance.sourceOperator );
    m_statusLabel->setText( provenance );

    if ( m_count > 0 )
        m_spectrumList->setCurrentRow( 0 );
    return true;
}

void SpectralWorkbenchPanel::selectSpectrum( int index )
{
    if ( m_count <= 0 )
        return;
    index = std::clamp( index, 0, m_count - 1 );
    m_spectrumList->setCurrentRow( index );
}

void SpectralWorkbenchPanel::refreshViews()
{
    m_spectrumList->clear();
    for ( const QString &label : m_labels )
        m_spectrumList->addItem( label );

    m_matrixView->clear();
    if ( m_count >= 1 && m_count <= kMaxMatrixRows )
    {
        std::vector<float> flat;
        flat.reserve( m_spectra.size() * static_cast<size_t>( m_bandCount ) );
        for ( const auto &row : m_spectra )
            flat.insert( flat.end(), row.begin(), row.end() );

        std::vector<double> matrix;
        QString error;
        if ( EndmemberAnalysis::angleMatrix( flat.data(), m_count, m_bandCount,
                                             &matrix, &error ) )
        {
            QString text;
            for ( int r = 0; r < m_count; ++r )
            {
                for ( int c = 0; c < m_count; ++c )
                {
                    if ( c )
                        text += QLatin1Char( ' ' );
                    text += QString::number(
                        matrix[static_cast<size_t>( r ) * m_count + c], 'f', 4 );
                }
                text += QLatin1Char( '\n' );
            }
            m_matrixView->setPlainText( text );
        }
        else
        {
            m_matrixView->setPlainText( error );
        }
    }

    m_detailsView->clear();
}

void SpectralWorkbenchPanel::onRowActivated( int row )
{
    if ( row < 0 || row >= m_count )
        return;
    QString text = tr( "%1 — %2 bands\n" ).arg( m_labels.at( row ) ).arg( m_bandCount );
    const std::vector<float> &spectrum = m_spectra[static_cast<size_t>( row )];
    for ( int b = 0; b < m_bandCount; ++b )
        text += QStringLiteral( "b%1: %2\n" ).arg( b + 1 ).arg( spectrum[b] );
    m_detailsView->setPlainText( text );
    emit spectrumSelected( m_labels.at( row ), row );
}
